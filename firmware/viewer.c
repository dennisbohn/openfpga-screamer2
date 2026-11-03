/*
 * The viewer (viewer.h)
 */
#include "viewer.h"
#include "frame.h"
#include "files.h"
#include "zip.h"
#include "hw.h"
#include "console.h"
#include "dlog.h"
#include "raster.h"
#include "blit.h"
#include "screen.h"
#include "slot.h"
#include "track.h"
#include "track_build.h"
#include "road.h"

/* The SDRAM (docs/soc.md): the track image where Screamer 1's was, the game's containers above it (the
   ZIP's entries read whole), the build's scratch up to the program */
#define TEX_FILE    ((const volatile uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))   /* the image, up to 7 MB */
#define TEX_MAX     0x700000u
#define GFX_MEM     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000))                  /* GFX/MAPnH01.DAT */
#define GFX_MAX     0x400000u
#define CARS_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2C00000))                  /* GFX/CARS101.DAT */
#define CARS_MAX    0x100000u
#define DATA_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D00000))                  /* DATA/MAPn.DAT */
#define DATA_MAX    0x40000u
#define SCRATCH_MEM ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000u))

/* the simulation's own slots (core/data.json does not have them; tools/sim.sh): a finished image offered
   instead of the build, the frames to run, the track */
#define IMAGE_SLOT  30
#define RACE_SLOT   29
#define TRACK_SLOT  28

#define TRACKS      7
#define SPEED       9265.0f             /* 150 km/h in units a second (drive_kmh: speed x 340 / 21000) */
#define STEP_S      0.02f               /* a video frame */
#define MS_CYCLES   (CPU_HZ / 1000)
#define LATE_CYCLES (CPU_HZ / 50)       /* a frame over 20 ms misses its vertical blanking */

static const struct rs_clip clip = { 0, 0, FB_W, FB_H };
static struct blit_list back;

/* the screen's palette for the loading and error screens: entries 0 to 4 (files.c's) */
#define BLACK   0
#define WHITE   1
#define GREEN   2
#define RED     3
#define GREY    4

static uint32_t sim_frames;             /* in the simulation: the frames to draw before it ends */
static uint32_t results;
static int sim;

/* "10.4" for cycles, to one decimal of a millisecond */
static char *fmt_ms(char *p, uint32_t cycles)
{
    uint32_t t = (cycles + 3712) / 7425;
    p = fmt_udec(p, t / 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    *p = 0;
    return p;
}

static void screen_text(int n, const char *l1, const char *l2, const char *l3, int err)
{
    static const uint32_t pal[5] = { 0x000000, 0xFFFFFF, 0x40D070, 0xF04848, 0x909090 };
    uint32_t st = wait_swap_done();
    volatile uint8_t *fb = FB(!(st & VID_FRONT));
    for (int i = 0; i < 5; i++) VID_PAL[i] = pal[i];
    fb_fill(fb, 0, 0, FB_W, FB_H, BLACK);
    fb_fill(fb, 0, 0, FB_W, 8, WHITE);
    con_text(fb, 1, 0, "SCREAMER 2 - TRACK VIEWER", BLACK, WHITE);
    char line[32];
    fmt_udec(fmt_str(line, "TRACK "), (uint32_t)n);
    con_text(fb, 14, 10, line, err ? RED : WHITE, BLACK);
    if (l1) con_text(fb, 20 - (int)__builtin_strlen(l1) / 2, 12, l1, err ? RED : GREEN, BLACK);
    if (l2) con_text(fb, 20 - (int)__builtin_strlen(l2) / 2, 13, l2, WHITE, BLACK);
    if (l3) con_text(fb, 20 - (int)__builtin_strlen(l3) / 2, 14, l3, GREY, BLACK);
    con_text(fb, 1, 22, "d-pad left/right: track", GREY, BLACK);
    con_text(fb, 1, 23, "A: cars 4 / 0 / 1", GREY, BLACK);
    con_text(fb, 1, 24, "B: timing overlay", GREY, BLACK);
    VID_SWAP = 1;
}

/* a ZIP entry whole to dest; 1 if that worked. The log gets its size and how long it took */
static int read_entry(struct zip *z, const char *path, uint8_t *dest, uint32_t max, uint32_t *size)
{
    const struct zip_entry *e = zip_find(z, path);
    dlog_str("viewer: ");
    dlog_str(path);
    if (!e) {
        dlog_str(" is not in the ZIP\n");
        return 0;
    }
    uint32_t t0 = rdcycle();
    int r = zip_read(z, e, dest, max);
    dlog_char(' ');
    dlog_dec((int32_t)e->size);
    dlog_str(" bytes, method ");
    dlog_dec(e->method);
    if (r) {
        dlog_str(": read failed (zip result ");
        dlog_dec(r);
        dlog_str(")\n");
        return 0;
    }
    dlog_str(", read in ");
    dlog_dec((int32_t)((rdcycle() - t0) / MS_CYCLES));
    dlog_str(" ms\n");
    *size = e->size;
    return 1;
}

/* the simulation's short cut (tools/simcheck.sh, SIM_IMAGE): the finished image as data slot IMAGE_SLOT,
   which core/data.json does not have; its size is 0 on the Pocket */
static int sim_image(void)
{
    uint32_t size = slot_size(IMAGE_SLOT), body = size & ~3u;
    if (!size || size > TEX_MAX) return 0;
    if (body && slot_read(IMAGE_SLOT, 0, (uint32_t)(uintptr_t)TEX_FILE, body)) return 0;
    if (size > body && slot_read(IMAGE_SLOT, body, (uint32_t)(uintptr_t)TEX_FILE + body, SLOT_REST)) return 0;
    dlog_str("viewer: track image from the testbench\n");
    return track_open(TEX_FILE);
}

/* Track n: its containers from the ZIP, the image built, checked and set up, the road chained. 1 if all
   worked, else 0 and what stopped it in *what. The cars' container is read once. */
static int load_track(struct zip *z, int n, const char **what)
{
    static uint32_t cars_size;
    static struct s2_files f;
    static char gfx_name[24], data_name[24];
    struct track_build_info info;
    uint32_t t0 = rdcycle();
    dlog_str("viewer: loading track ");
    dlog_dec(n);
    dlog_char('\n');
    if (sim && slot_size(IMAGE_SLOT)) {
        if (!sim_image()) return *what = "the image", 0;
    } else {
        fmt_str(fmt_udec(fmt_str(gfx_name, "GFX/MAP"), (uint32_t)n), "H01.DAT");
        fmt_str(fmt_udec(fmt_str(data_name, "DATA/MAP"), (uint32_t)n), ".DAT");
        if (!cars_size && !read_entry(z, "GFX/CARS101.DAT", CARS_MEM, CARS_MAX, &cars_size)) return *what = "GFX/CARS101.DAT", 0;
        f.cars = CARS_MEM;
        f.cars_size = cars_size;
        f.gfx = GFX_MEM;
        f.data = DATA_MEM;
        if (!read_entry(z, gfx_name, GFX_MEM, GFX_MAX, &f.gfx_size)) return *what = "the track's GFX file", 0;
        if (!read_entry(z, data_name, DATA_MEM, DATA_MAX, &f.data_size)) return *what = "the track's DATA file", 0;
        uint32_t size = track_build_s2(&f, n, (uint8_t *)(uintptr_t)TEX_FILE, TEX_MAX, SCRATCH_MEM, SCRATCH_MAX, &info);
        while (!(MEM_STATUS & MEM_WRITES_IDLE))     /* all of it in the SDRAM for the 3D units */
            ;
        dcache_invalidate();                        /* the block check reads the SDRAM */
        if (!size || !track_open(TEX_FILE)) {
            dlog_str("viewer: image not built, stopped at ");
            dlog_str(size ? "its header" : info.fail);
            dlog_char('\n');
            *what = size ? "the image header" : "the image build";
            return 0;
        }
        dlog_str("viewer: image ");
        dlog_dec((int32_t)size);
        dlog_str(" bytes built in ");
        dlog_dec((int32_t)(info.cycles / MS_CYCLES));
        dlog_str(" ms (unpacking ");
        dlog_dec((int32_t)(info.unpack_cycles / MS_CYCLES));
        dlog_str(" ms)\n");
    }
    int bad = 0;
    uint32_t t1 = rdcycle();
    for (int k = 0; k < track_blocks(); k++) bad += !track_block_ok(k);
    dlog_str("viewer: image CRC of the 256 KB blocks ");
    dlog_str(bad ? "BAD in " : "all ok in ");
    dlog_dec((int32_t)((rdcycle() - t1) / MS_CYCLES));
    dlog_str(" ms\n");
    if (bad) return *what = "the image CRC", 0;
    int placements = track_init();
    if (!placements) return *what = "the track limits", 0;
    uint32_t t2 = rdcycle();
    int points = road_build();
    if (!points) return *what = "the road", 0;
    dlog_str("viewer: track ");
    dlog_dec(n);
    dlog_str(": ");
    dlog_dec(placements);
    dlog_str(" placements, road ");
    dlog_dec(points);
    dlog_str(" points ");
    dlog_dec((int32_t)road_length());
    dlog_str(" units (");
    dlog_dec((int32_t)((rdcycle() - t2) / MS_CYCLES));
    dlog_str(" ms), ready after ");
    dlog_dec((int32_t)((rdcycle() - t0) / MS_CYCLES));
    dlog_str(" ms\n");
    return 1;
}

/* the frames of a visit (a track with a number of cars): what the overlay, the log and debug.txt's first line show */
struct stats {
    uint64_t sum;                       /* cycles of all the frames' work */
    uint32_t n, max, over;              /* frames, the longest, the ones over 20 ms */
    uint32_t faces, pieces;             /* sums over the frames */
    float worst_s;                      /* where the longest frame was: units along the road */
    uint32_t vf0;                       /* the video frame count at the first frame */
};

static void stats_text(char *p, int n, int cars, const struct stats *s, uint32_t vf, int full)
{
    uint32_t dvf = (vf - s->vf0) & 0xFFFF, fps10 = dvf ? s->n * 500 / dvf : 0;
    char *q = fmt_udec(fmt_str(fmt_udec(fmt_str(p, full ? "viewer: track " : "Screamer Pocket T"), (uint32_t)n),
                               full ? ", cars " : " c"), (uint32_t)cars);
    q = fmt_udec(fmt_str(q, full ? ", frames " : " fr"), s->n);
    q = fmt_str(q, full ? ", ms mean " : " avg");
    q = fmt_ms(q, s->n ? (uint32_t)(s->sum / s->n) : 0);
    q = fmt_str(q, full ? " max " : " max");
    q = fmt_ms(q, s->max);
    if (full) {
        q = fmt_udec(fmt_str(q, " at "), (uint32_t)(s->worst_s / 1000.0f));
        q = fmt_str(q, "k");
    }
    q = fmt_udec(fmt_str(q, full ? ", over 20 ms " : " late"), s->over);
    q = fmt_str(q, full ? ", " : " fps");
    q = fmt_udec(q, fps10 / 10);
    *q++ = '.';
    *q++ = (char)('0' + fps10 % 10);
    *q = 0;
    if (full) {
        q = fmt_udec(fmt_str(q, "/s, faces "), s->n ? s->faces / s->n : 0);
        q = fmt_udec(fmt_str(q, ", pieces "), s->n ? s->pieces / s->n : 0);
    }
}

static void log_stats(int n, int cars, const struct stats *s, uint32_t vf, const char *tag)
{
    char buf[160];
    stats_text(buf, n, cars, s, vf, 1);
    dlog_str(tag);
    dlog_str(buf);
    dlog_char('\n');
}

/* A visit to track n until the player picks another (its number), or a button changes the cars or the
   overlay (they stay). The image is set up (load_track). */
static int visit(int n, int *cars, int *overlay)
{
    const uint32_t *pal = track_palette();
    const uint8_t col_text = nearest((const volatile uint32_t *)pal, 255, 255, 255);
    const uint8_t col_bg = nearest((const volatile uint32_t *)pal, 0, 0, 0);
    track_mode = TRACK_HW;
    track_hw_piece = hw_piece;
    GU_MLB = (uint32_t)((uintptr_t)track_mlb() - SDRAM_BASE);
    GU_INVAL = 1;
    RS_CTRL = 1;
    static float resume_s;                              /* where the road was left when only the cars changed */
    static int timeline_done;
    uint32_t keys_before = in_keys(), last_vf = 0, drawn = 0, window_vf = 0, next_log = 5 * VID_HZ;
    struct stats st_all = { 0 };
    float s = resume_s;
    resume_s = 0.0f;
    uint32_t win_n = 0, win_sum = 0, win_max = 0, shown_mean = 0, shown_max = 0, shown_faces = 0;
    for (;;) {
        uint32_t st = wait_swap_done(), t_frame = rdcycle(), vf = VID_FRAME(st);
        uint32_t keys = in_keys(), press = keys & ~keys_before;
        keys_before = keys;
        tl_begin(t_frame);
        frames_shown = drawn;                           /* every frame drawn so far has been swapped in */
        if (sim && drawn == sim_frames) {               /* the simulation ends: the last frame is up */
            SYS_SIM = SIM_DONE | results;
            dlog_str("SYS_SIM=");
            dlog_hex(SIM_DONE | results);
            dlog_char('\n');
        }
        if (drawn == 0) {
            st_all.vf0 = vf;
            window_vf = vf;
        }
        /* a button: another track (this visit ends), the cars, the overlay */
        if (press & (KEY_LEFT | KEY_RIGHT)) {
            log_stats(n, *cars, &st_all, vf, "left: ");
            return (press & KEY_RIGHT) ? n % TRACKS + 1 : (n + TRACKS - 2) % TRACKS + 1;
        }
        if (press & KEY_A) {
            log_stats(n, *cars, &st_all, vf, "left: ");
            *cars = *cars == 4 ? 0 : *cars == 0 ? 1 : 4;
            resume_s = s;
            return -n;                                  /* the same track, the statistics from the start */
        }
        if (press & KEY_B) *overlay = !*overlay;

        /* the camera moves with real time: a video frame is 1/50 s whatever the frame took */
        uint32_t steps = drawn ? (vf - last_vf) & 0xFFFF : 1;
        last_vf = vf;
        if (steps > 10) steps = 10;
        s += (float)steps * STEP_S * SPEED;
        if (s > road_length()) s -= road_length();
        struct track_cam cam;
        static struct track_object objs[ROAD_CARS];
        int nobj = road_scene(s, *cars, &cam, objs);
        tl_mark(TL_PHYS);
        struct su_view view;
        track_view(&clip, &view);

        /* the backdrop (the game's 0x83a68): the blitter copies it while the CPU culls the scene; the first
           piece for the geometry unit waits until it is done (hw_piece) */
        volatile uint8_t *fb = FB(!(st & VID_FRONT));
        draw_buf = !(st & VID_FRONT);
        blit_start(&back, (int)draw_buf);
        track_backdrop_blit(&back, &clip, &cam);
        BL_CLOCKS = 0;
        back_clocks = back_cycles = 0;
        back_wait = 1;
        hw_blit(&back);
        pal_faded(pal, 0x8000);

        /* the pieces from the CPU to the geometry unit, its faces to the setup unit, the triangles to the
           rasterizer */
        struct track_stats ts;
        RS_PIXELS = 0;
        RS_MISSES = 0;
        RS_CLOCKS = 0;
        SU_CLOCKS = 0;
        GU_CLOCKS = 0;
        GU_FACES = 0;
        GU_MODE = draw_buf << 8;
        draw_wait = 0;
        tl_mark(TL_PREP);
        RS_CLIP0 = RS_XY(clip.x0, clip.y0);
        RS_CLIP1 = RS_XY(clip.x1, clip.y1);
        su_view_regs(&view);
        tl_scene = 1;
        track_draw_cam(0, &clip, &cam, objs, nobj, &ts);
        tl_scene = 0;
        tl_mark(TL_SCENE);
        while (RS_DRAW & RS_BUSY)
            ;
        tl_mark(TL_WAIT);
        uint32_t work = rdcycle() - t_frame, faces = GU_FACES;

        /* the timing, drawn over the picture once the rasterizer is done */
        if (*overlay) {
            char l[40], *p;
            p = fmt_ms(l, shown_mean);
            p = fmt_ms(fmt_str(p, " ms  max "), shown_max);
            fmt_str(p, " ms");
            con_text(fb, 0, 0, l, col_text, col_bg);
            p = fmt_udec(fmt_str(l, "T"), (uint32_t)n);
            p = fmt_udec(fmt_str(p, "  cars "), (uint32_t)*cars);
            p = fmt_udec(fmt_str(p, "  faces "), shown_faces);
            con_text(fb, 0, 1, l, col_text, col_bg);
        }
        tl_mark(TL_HUD);

        /* the statistics of the visit and of the last second */
        st_all.sum += work;
        st_all.n++;
        st_all.faces += faces;
        st_all.pieces += ts.pieces;
        st_all.over += work > LATE_CYCLES;
        if (work > st_all.max) {
            st_all.max = work;
            st_all.worst_s = s;
        }
        win_n++;
        win_sum += work > 0x20000000u ? 0x20000000u : work;
        if (work > win_max) win_max = work;
        shown_faces = faces;
        if (((vf - window_vf) & 0xFFFF) >= VID_HZ && win_n) {
            shown_mean = win_sum / win_n;
            shown_max = win_max;
            win_n = win_sum = win_max = 0;
            window_vf = vf;
        }
        if (drawn % 25 == 0) {
            char head[96];
            stats_text(head, n, *cars, &st_all, vf, 0);
            dlog_head(head);                            /* debug.txt's first line, live */
        }
        uint32_t since = (vf - st_all.vf0) & 0xFFFF;
        if (sim ? drawn + 1 == sim_frames : (since >= next_log && since < 0x8000)) {
            log_stats(n, *cars, &st_all, vf, "");
            if (!timeline_done) {                       /* the previous frame's timeline, once: it is a kilobyte */
                timeline_done = 1;
                tl_log();
            }
            next_log = since + 60 * VID_HZ;
            if (sim) {
                dlog_str("stack used ");
                dlog_dec((int32_t)stack_used());
                dlog_str(" bytes\n");
            }
        }
        tl_mark(TL_END);
        VID_SWAP = 1;
        drawn++;
    }
}

void __attribute__((noreturn)) viewer_run(uint32_t boot_results)
{
    struct zip *z = files_zip();
    results = boot_results;
    sim = (SYS_FLAGS & FLAG_SIM) != 0;
    sim_frames = sim ? (slot_size(RACE_SLOT) ? slot_size(RACE_SLOT) : 8) : 0;
    int n = sim && slot_size(TRACK_SLOT) ? (int)slot_size(TRACK_SLOT) : 1, cars = 4, overlay = 1;
    dlog_str("viewer: start\n");
    for (;;) {
        const char *what = "";
        screen_text(n, "Loading...", "the track is built from the ZIP", 0, 0);
        if (load_track(z, n, &what)) {
            int next;
            while ((next = visit(n, &cars, &overlay)) < 0)
                ;                                       /* the cars changed: the same visit again */
            n = next;
        } else {                                        /* what stopped it is in the log; left and right try another */
            char l[40];
            fmt_str(fmt_str(l, "Could not set up "), what);
            screen_text(n, l, "see debug.txt in the Saves folder", "d-pad left/right: another track", 1);
            uint32_t keys_before = in_keys();
            for (;;) {
                wait_swap_done();
                uint32_t keys = in_keys(), press = keys & ~keys_before;
                keys_before = keys;
                if (press & KEY_RIGHT) { n = n % TRACKS + 1; break; }
                if (press & KEY_LEFT) { n = (n + TRACKS - 2) % TRACKS + 1; break; }
            }
        }
    }
}
