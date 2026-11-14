/*
 * The program (program.bin, data slot 2) in the SDRAM, started by the boot loader with its
 * results (docs/soc.md). It reads the saves, checks and lists the game's files (files.c: the
 * ZIP, the music image and its cue sheet, each [OK] or [MISSING]) and, when nothing is missing
 * and the read test passed, hands over to the game (viewer.c). Here too: what the game's code
 * and the hardware share, the hand-over of pieces to the geometry unit and of commands to the
 * blitter, the palette's fade, the frame's timeline.
 */
#include "hw.h"
#include "console.h"
#include "dlog.h"
#include "raster.h"
#include "setup.h"
#include "geom.h"
#include "blit.h"
#include "files.h"
#include "screen.h"
#include "music.h"
#include "save.h"
#include "s2save.h"
#include "frame.h"
#include "viewer.h"
#include "prof.h"

uint32_t frames_shown;            /* read by the trap handler */

#define STACK_FILL  0x57AC57ACu /* marks unused stack, for the high-water mark */
#define STACK_LOW   0x6000u     /* the stack may grow down to here (link.ld keeps the boot loader below) */

static uint32_t results;        /* SYS_SIM bits 20..0, from the boot loader */

#define BUILD_SCRATCH   (SDRAM_BASE + 0x2D60000)

/* Mark the free stack in the boot RAM up to 256 bytes below the stack pointer, find how
   deep it was used later */
static void __attribute__((noinline)) stack_fill(void)
{
    uintptr_t sp;
    __asm__ volatile ("mv %0, sp" : "=r"(sp));
    for (uint32_t *p = (uint32_t *)STACK_LOW; (uintptr_t)p < sp - 256; p++)
        *p = STACK_FILL;
}

uint32_t stack_used(void)
{
    uint32_t *p = (uint32_t *)STACK_LOW;
    while (*p == STACK_FILL)
        p++;
    return BOOTRAM_SIZE - (uint32_t)(uintptr_t)p;
}

void log_head(const char *what, uint32_t n)
{
    char buf[DLOG_HEAD + 16];
    fmt_udec(fmt_str(fmt_str(buf, "Screamer Pocket step 3, "), what), n);
    dlog_head(buf);
}

void (*hw_idle)(void);                  /* frame.h */
uint32_t draw_buf;                      /* the back buffer, for the hardware's draw commands (frame.h) */
uint32_t draw_wait;                     /* cycles the DRAW writes waited for the rasterizer */
int back_wait;                          /* the backdrop is with the blitter: the first piece waits */
uint32_t back_clocks, back_cycles;      /* the blitter's clocks for it, the CPU's wait */

/* The frame's timeline (tools/make_timeline_svg.py draws it): at each mark the cycles since
   the frame began, the busy clocks of the geometry unit, setup unit, rasterizer and blitter
   and the CPU's cycles writing to them, each as its counter holds it (cleared during the
   frame; a mark follows every clear). Two buffers: the last whole frame is logged. */
#define TL_MAX 48
#define TL_STRIDE 12                  /* a mark every TL_STRIDE pieces: the debug log has 3,840 bytes for everything and a mark takes 36 of them (more than 8 pieces
                                           between marks since the effects and the scenery: 254 pieces a frame made the timeline fill the log and cut the lines after it) */
static struct tl_mark { uint32_t t, gu, su, rs, bl, wait, tag; } tl[2][TL_MAX];
static uint32_t tl_n[2], tl_cur, tl_t0, tl_pieces;
uint32_t tl_scene;                      /* hw_piece marks the pieces while this is set (frame.h) */

void tl_mark(uint32_t tag)
{
    uint32_t n = tl_n[tl_cur];
    if (n < TL_MAX) {
        tl[tl_cur][n] = (struct tl_mark){ rdcycle() - tl_t0, GU_CLOCKS, SU_CLOCKS, RS_CLOCKS, BL_CLOCKS,
                                          draw_wait, tag };
        tl_n[tl_cur] = n + 1;
    }
}

void tl_begin(uint32_t t_frame)
{
    tl_cur ^= 1;
    tl_t0 = t_frame;
    tl_pieces = 0;
    tl[tl_cur][0] = (struct tl_mark){ 0, 0, 0, 0, 0, 0, TL_START };    /* the units idle: the last frame waited */
    tl_n[tl_cur] = 1;
}

void tl_log(void)
{
    const uint32_t c = tl_cur ^ 1;
    dlog_str("  timeline (t gu su rs bl wait tag):");
    for (uint32_t i = 0; i < tl_n[c]; i++) {
        const struct tl_mark *m = &tl[c][i];
        const uint32_t v[7] = { m->t, m->gu, m->su, m->rs, m->bl, m->wait, m->tag };
        dlog_str(i % 6 ? " |" : "\n   ");
        for (int k = 0; k < 7; k++) {
            dlog_char(' ');
            dlog_dec((int32_t)v[k]);
        }
    }
    dlog_char('\n');
}

/* the projection into the setup unit's registers */
void su_view_regs(const struct su_view *v)
{
    SU_CENTRE = RS_XY(v->cx, v->cy);
    SU_SCALE = v->scale;
    SU_FAR = (uint32_t)v->far8;
}

void smg_load(const uint8_t *table)
{
    for (uint32_t a = 0; a < 16384; a++)
        RS_SMG = a << 8 | table[a];
    rs_smg = table;                             /* (the model's copy, raster.h) */
}

static void push_piece(const struct gu_piece *g);

/* a piece into the geometry unit's buffer: its place in view space and its mesh; a word
   waits only while the buffer is full. A block over the unit's limits (512 vertices, 1,024
   faces a row: geom.h) goes in as the runs of its face row, one after the other (gu_split). */
void hw_piece(const struct gu_piece *g)
{
    if (!gu_oversize(g)) {
        push_piece(g);
        return;
    }
    static struct gu_piece run[GU_CUT_RUNS];
    static uint32_t failures_told;
    const int n = gu_split(g, run);
    while (!(MEM_STATUS & MEM_WRITES_IDLE))     /* (the runs just cut are in the SDRAM for the unit) */
        ;
    for (int i = 0; i < n; i++) push_piece(&run[i]);
    if (gu_cut_failures != failures_told) {
        failures_told = gu_cut_failures;
        dlog_str("geom: a block over the unit's limits could not be cut, it is not drawn\n");
    }
}

static void push_piece(const struct gu_piece *g)
{
    if (tl_scene) {
        if (tl_pieces++ == 0)
            tl_mark(TL_CULL);
        else if (tl_pieces % TL_STRIDE == 1)
            tl_mark(TL_PIECES);
    }
    uint32_t t0 = rdcycle();
    if (back_wait) {                    /* the scene goes over the backdrop */
        while (RS_DRAW & RS_BUSY)
            ;
        back_clocks = BL_CLOCKS;
        back_cycles = rdcycle() - t0;
        back_wait = 0;
        t0 = rdcycle();
    }
    for (int k = 0; k < 9; k++) GU_PUSH = (uint32_t)g->m[k];
    for (int k = 0; k < 3; k++) GU_PUSH = (uint32_t)g->t[k];
    for (int k = 0; k < 3; k++) GU_PUSH = (uint32_t)g->e[k];
    GU_PUSH = (uint32_t)((uintptr_t)g->obj - SDRAM_BASE);
    GU_PUSH = (uint32_t)((uintptr_t)g->blk - SDRAM_BASE);
    GU_PUSH = ((uint32_t)g->row & 7) | (uint32_t)(g->mirror != 0) << 3 | ((uint32_t)g->row & 8) << 1 |
              (g->phase & 0x3FFF) << 16;
    GU_PUSH = g->refl;
    uint32_t waited = rdcycle() - t0;
#ifdef S2_PROF
    prof_push(waited);
#endif
    draw_wait += waited;
    if (waited > HW_FULL_CYCLES && hw_idle)
        hw_idle();
}

/* the palette entry nearest to an RGB colour, for text on the track's palette */
uint8_t pal_nearest(const volatile uint32_t *pal, int r, int g, int b)
{
    uint32_t best = ~0u;
    uint8_t n = 0;
    for (int i = 0; i < 256; i++) {
        int dr = (int)(pal[i] >> 16 & 255) - r, dg = (int)(pal[i] >> 8 & 255) - g, db = (int)(pal[i] & 255) - b;
        uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
        if (d < best) { best = d; n = (uint8_t)i; }
    }
    return n;
}

/* the blitter's commands (blit.v): a copy's first word is its SDRAM byte address */
void hw_blit(const struct blit_list *l)
{
    for (int i = 0; i < l->n; i++) {
        const uint32_t *c = l->c[i];
        uint32_t kind = c[1] >> 28 & 3;
        BL_PUSH = kind == BLIT_COPY || kind == BLIT_FONT ? (uint32_t)((uintptr_t)l->src[i] - SDRAM_BASE) : c[0];
        BL_PUSH = c[1];
        BL_PUSH = c[2];
        BL_PUSH = c[3];
    }
}

/* pal at f of 0x8000 into the back buffer's palette: towards black, as the game's 0x27de8 */
void pal_faded(const uint32_t *pal, int32_t f)
{
    uint32_t k = f < 0 ? 0 : f > 0x8000 ? 0x8000 : (uint32_t)f;
    if (k == 0x8000) {                                  /* not faded: the same colours (the race draws this every frame) */
        for (int i = 0; i < 256; i++) VID_PAL[i] = pal[i] & 0xFFFFFFu;
        return;
    }
    for (int i = 0; i < 256; i++) {
        uint32_t c = pal[i];
        VID_PAL[i] = ((c >> 16 & 255) * k >> 15) << 16 | ((c >> 8 & 255) * k >> 15) << 8 | (c & 255) * k >> 15;
    }
}

/* The master volumes of the save (s2save.h; the game's 0x2774d: digital, CD, MIDI) to the music: the CD's and the FM music's
   (the effects take theirs when they play, s2sound.c) */
static void volumes(void)
{
    music_master(s2save_get(S2S_VOL_CD));
    music_master_mu(s2save_get(S2S_VOL_FM));
}

/* The list of the game's files (files.c): the swaps go on, and the simulation gets its result after three
   frames. When nothing is missing and the read test passed, the viewer (viewer.h) starts from here: at a
   press of A, B or START, after 5 s, in the simulation after the third frame. failed: they are all there,
   but loading them failed (then for ever) */
static void __attribute__((noreturn)) files_loop(int failed)
{
    uint32_t drawn = 0;
    for (;;) {
        uint32_t st = wait_swap_done();
        frames_shown = drawn;       /* every frame drawn so far has been swapped in */
        int go = !failed && files_ready();
        if (go && drawn >= 3)       /* the first pictures are up (the LOADING screen of files_draw) and the read test passed: on at once, the viewer brings up its own loading picture and starts with the intro */
            viewer_run(results);
        if (drawn == 3 && !go) {    /* three frames drawn and swapped */
            SYS_SIM = SIM_DONE | results;
            dlog_str("stack used ");
            dlog_dec((int32_t)stack_used());
            dlog_str(" bytes\nSYS_SIM=");
            dlog_hex(SIM_DONE | results);
            dlog_char('\n');
        }
        if (drawn == 1)             /* the first picture is up (with the notice for a compressed ZIP): the read test */
            files_load_check((uint8_t *)(SDRAM_BASE + 0x1000000), 0x800000, (SYS_FLAGS & FLAG_SIM) ? 0x19000 : 0xFFFFFFFFu);
        files_draw(FB(!(st & VID_FRONT)), failed);
        VID_SWAP = 1;
        log_head("frames drawn: ", ++drawn);
    }
}

void prog_main(uint32_t boot_results, uint32_t log_pos, uint32_t log_cur)
{
    results = boot_results;
    stack_fill();
    dlog_attach(log_pos, log_cur);
    log_head("frames drawn: ", 0);
    dlog_str("program.bin: running in the SDRAM\n");
    save_load();                    /* first: until then an exit would save the SDRAM as it is */
    volumes();
    /* the ZIP of the game's files is read and listed (the status screen); when nothing is missing
       and the read test passed, the viewer (viewer.h) goes on from there */
    files_check((uint8_t *)BUILD_SCRATCH, PROG_BASE - BUILD_SCRATCH);
    files_loop(0);
}
