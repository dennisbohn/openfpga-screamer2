/*
 * WELCOME's flying plates (firmware/s2plates.c, the page in s2leaguepages.c) against the running original: tools/plates.sh gives this the recording of
 * `python -I tools/s2/dosbox_desk.py welcome` (tools/desk_shots.py: a row for every change of the plates' records 0x131290 + 0x4c i and the four engine objects' records) and its raw
 * screenshots (320 x 200 palette indices, tools/pages3d_idx.py). Two parts:
 *
 *  1. The states. The firmware's flight is run from the first frame (s2plates_begin(4), s2plates_step() a frame); every recorded row is looked for among those frames: the four plates'
 *     object records (place, angles) and their records' time u, phase and finished mark all equal to those of one frame. A row whose plates are not all of one frame is one the poll
 *     read while the game was writing it: it is "torn" if each plate is as it is in the frame f or the next, else it is wrong. The frame in which the page ends (the first with
 *     the fade's flag set, 0x132498) is compared with the frame in which s2plates_step() says the flight is over.
 *  2. The pictures. For each screenshot the states round the moment of its request (every distinct row in a window of 0.16 s) are drawn as the firmware's WELCOME page draws them (the
 *     league's picture, the plates, the dark band and its lines) and compared pixel for pixel with the screenshot; the best state is its frame. Where the screenshot's picture or
 *     ours differs from the plain picture below the band (the plates' pixels, either side) it counts how many are equal and how many of the rest have the other's colour within
 *     a pixel (the original fills textures affine, the unit is perspective-correct: another texel is picked on a plate seen at a slant).
 *
 * Usage: plates_match <SCREAMER2.zip> <rows file> <shots file> <folder of .idx> [<out prefix: pictures of the best match per screenshot (DUMP=<part of a name>)>]
 */
#define _GNU_SOURCE
#include "../hw.h"
#include "../s2scene.h"
#include "../s2gfx.h"
#include "../s2menu.h"
#include "../s2page.h"
#include "../s2save.h"
#include "../slot.h"
#include "../dlog.h"
#include "../dat.h"
#include "../geom.h"
#include "../raster.h"
#include "../frame.h"
#include "../s2sound.h"
#include "../files.h"
#include "../trackmem.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define SCRATCH     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000))

static FILE *zf;
static uint32_t zsize;
static struct zip zip;

uint32_t slot_size(uint32_t id) { return id == 3 ? zsize : 0; }
void slot_set_size(uint32_t id, uint32_t size) { (void)id; (void)size; }
#define OUTSIDE 0xE0000000u
static uint8_t *outside;
uint32_t zip_addr(const void *p)
{
    if ((uintptr_t)p >= SDRAM_BASE && (uintptr_t)p < SDRAM_BASE + SDRAM_SIZE) return (uint32_t)(uintptr_t)p;
    outside = (uint8_t *)p;
    return OUTSIDE;
}
int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    void *to = addr == OUTSIDE ? outside : (void *)(uintptr_t)addr;
    if (id != 3 || (off & 3) || off >= zsize || (len != SLOT_REST && ((len & 3) || off + len > zsize))) return 2;
    uint32_t n = len == SLOT_REST ? zsize - off : len;
    memset(to, 0, (n + 3) & ~3u);
    return fseek(zf, off, SEEK_SET) || fread(to, 1, n, zf) != n ? 2 : 0;
}

/* the hardware's stand-ins (as tools/host/room_match.c) */
int back_wait;
uint32_t draw_buf, draw_wait, back_clocks, back_cycles, tl_scene, frames_shown;
void (*hw_idle)(void);
uint32_t wait_swap_done(void) { return 0; }
void hw_blit(const struct blit_list *l) { blit_model(FB(l->buffer), l); }
void pal_faded(const uint32_t *pal, int32_t f) { (void)pal; (void)f; }
void su_view_regs(const struct su_view *v)
{
    SU_CENTRE = RS_XY(v->cx, v->cy);
    SU_SCALE = v->scale;
    SU_FAR = (uint32_t)v->far8;
}
static unsigned pieces_drawn, faces_drawn;
void hw_piece(const struct gu_piece *g)
{
    const struct su_view v = { (int16_t)(SU_CENTRE & 0xFFFF), (int16_t)(SU_CENTRE >> 16), SU_SCALE, (int32_t)SU_FAR };
    const struct rs_clip clip = { (int)(RS_CLIP0 & 0xFFFF), (int)(RS_CLIP0 >> 16), (int)(RS_CLIP1 & 0xFFFF), (int)(RS_CLIP1 >> 16) };
    struct gu_stats st = { 0, 0, 0, 0 };
    gu_draw(FB((GU_MODE >> 8) & 1), &clip, &v, (const uint8_t *)(uintptr_t)(SDRAM_BASE + GU_MLB), g, &st);
    pieces_drawn++;
    faces_drawn += st.faces;
}
void smg_load(const uint8_t *table) { rs_smg = table; }
void tl_mark(uint32_t t) { (void)t; }
void tl_begin(uint32_t t) { (void)t; }
void s2sound_menu(int id) { (void)id; }
void s2sound_effects(int32_t f) { (void)f; }
void music_frame(void) {}
int s2page_run(const struct s2_page *p, void *self) { (void)p; (void)self; return 0; }
struct viewer_image loaded;
struct zip *files_zip(void) { return &zip; }

#include "../s2plates.c"                                        /* (included: the test reaches the plates and the page's drawing) */
#include "../s2leaguepages.c"

static uint8_t frame_idx[S2G_PIC_BYTES];
static uint32_t shot_pal[256];

/* ------------------------------------------------------------------------------------------------ the rows */

#define MAX_FRAMES 700
struct pstate { int32_t pos[3], a[3]; float u; int phase, done; };
static struct pstate model[MAX_FRAMES][4];
static int model_frames, model_end = -1;

struct rrow { int fade, level, hund; int32_t v[4][9]; };          /* x y z a0 a1 a2, u's bits, phase, live */

static int same(const struct pstate *s, const int32_t *v)
{
    uint32_t ub;
    memcpy(&ub, &s->u, 4);
    return s->pos[0] == v[0] && s->pos[1] == v[1] && s->pos[2] == v[2] && s->a[0] == v[3] && s->a[1] == v[4] && s->a[2] == v[5] && ub == (uint32_t)v[6] && s->phase == v[7] && !s->done == v[8];
}

static void run_model(void)
{
    s2plates_begin(4);
    for (int f = 0; f < MAX_FRAMES; f++) {
        const int over = s2plates_step();
        for (int i = 0; i < 4; i++) {
            const struct s2plate *p = s2plates_get(i);
            model[f][i] = (struct pstate){ { p->pos[0], p->pos[1], p->pos[2] }, { p->a[0], p->a[1], p->a[2] }, p->u, p->phase, p->done };
        }
        model_frames = f + 1;
        if (over && model_end < 0) model_end = f;
        if (model_end >= 0 && f > model_end + 60) break;
    }
}

/* perturb: the negative control, every 50th row's first plate's x moved by one: the test must find them wrong */
static int part_states(const char *path, int perturb)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return 0; }
    static struct rrow rows[8000];
    int n = 0;
    while (n < 8000) {
        struct rrow *r = &rows[n];
        int ok = fscanf(f, "%d %d %d", &r->fade, &r->level, &r->hund) == 3;
        for (int i = 0; ok && i < 4; i++)
            for (int k = 0; k < 9; k++) ok = ok && fscanf(f, "%d", &r->v[i][k]) == 1;
        if (!ok) break;
        if (perturb && n % 50 == 0) r->v[0][0]++;
        n++;
        if (r->level == 0x3F800000) break;              /* (the page's fade is complete: what the recording has after it is the TRACK page's) */
    }
    fclose(f);
    int exact = 0, torn = 0, wrong = 0, first_wrong = -1, before = -1, flagged = -1;      /* the last frame seen with the fade's flag clear, the first with it set */
    for (int k = 0; k < n; k++) {
        const struct rrow *r = &rows[k];
        int found = -1;
        for (int fr = 0; fr < model_frames && found < 0; fr++) {
            int all = 1;
            for (int i = 0; i < 4; i++) all &= same(&model[fr][i], r->v[i]);
            if (all) found = fr;
        }
        if (found >= 0) {
            exact++;
            if (r->fade && flagged < 0) flagged = found;
            else if (!r->fade && flagged < 0 && found > before) before = found;
            continue;
        }
        int tornrow = 0, tfr = -1;                      /* (the poll reads the words one after the other: each word is that of the frame f or of the next) */
        for (int fr = 0; fr + 1 < model_frames && !tornrow; fr++) {
            int all = 1;
            for (int i = 0; i < 4 && all; i++) {
                uint32_t a[2][9];
                for (int s = 0; s < 2; s++) {
                    const struct pstate *m = &model[fr + s][i];
                    memcpy(&a[s][6], &m->u, 4);
                    a[s][0] = (uint32_t)m->pos[0]; a[s][1] = (uint32_t)m->pos[1]; a[s][2] = (uint32_t)m->pos[2];
                    a[s][3] = (uint32_t)m->a[0]; a[s][4] = (uint32_t)m->a[1]; a[s][5] = (uint32_t)m->a[2];
                    a[s][7] = (uint32_t)m->phase; a[s][8] = !m->done;
                }
                for (int k = 0; k < 9; k++) all &= (uint32_t)r->v[i][k] == a[0][k] || (uint32_t)r->v[i][k] == a[1][k];
            }
            if (all) tfr = fr;
            tornrow = all;
        }
        if (tornrow) {
            torn++;
            if (r->fade && flagged < 0) flagged = tfr + 1;
            else if (!r->fade && flagged < 0 && tfr > before) before = tfr;
        }
        else {
            wrong++;
            if (first_wrong < 0) first_wrong = k;
        }
    }
    if (perturb) {
        printf("negative control (every 50th row's first x moved by one): %d of %d rows are wrong, %d expected: %s\n", wrong, n, (n + 49) / 50, wrong >= (n + 49) / 50 ? "found" : "NOT FOUND");
        return wrong >= (n + 49) / 50;
    }
    printf("states: %d rows: %d are one frame of the firmware's flight exactly, %d are rows the poll read while the page was writing them (each plate is of a frame or the next), %d are wrong\n",
           n, exact, torn, wrong);
    if (first_wrong >= 0) printf("  the first wrong row is %d (hundredths %d)\n", first_wrong, rows[first_wrong].hund);
    const int inside = before <= model_end && model_end <= flagged;
    printf("the flight ends in frame %d of the firmware (the last plate's u %.3f); the original's flag of the end is clear in the rows up to frame %d and set from frame %d on%s\n", model_end,
           model_end >= 0 ? model[model_end][3].u : 0.0f, before, flagged, inside ? ": the firmware's frame is between them" : ": NOT between them");
    return wrong == 0 && inside;
}

/* ------------------------------------------------------------------------------------------------ the pictures */

static int diff(void)
{
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int i = 0; i < S2G_PIC_BYTES; i++) d += fb[i] != frame_idx[i];
    return d;
}

static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

static void render(const int32_t v[28])
{
    for (int i = 0; i < 4; i++) {
        struct s2plate *p = &plate[i];
        p->pos[0] = v[7 * i];
        p->pos[1] = v[7 * i + 1];
        p->pos[2] = v[7 * i + 2];
        p->a[0] = v[7 * i + 3];
        p->a[1] = v[7 * i + 4];
        p->a[2] = v[7 * i + 5];
        p->done = !v[7 * i + 6];
    }
    static int league = 0;
    struct blit_list *l = s2gfx_begin_on(0);
    pieces_drawn = faces_drawn = 0;
    s2gfx_picture(l, s2gfx_pic(0));
    welcome_draw(&league, l, 0);
    s2gfx_flush();
}

static void dump(const char *prefix, const char *name, const char *what)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s_%s.rgb", prefix, name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (!strcmp(what, "diff")) c = fb[i] != frame_idx[i] ? 0xFFFFFF : 0;
        else c = shot_pal[strcmp(what, "ref") ? fb[i] : frame_idx[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

struct cand { int32_t v[28]; };

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <rows file> <shots file> <folder of .idx> [out prefix]\n", argv[0]); return 2; }
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE) ||
        mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED ||
        mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    *(volatile uint32_t *)0xF000000Cu = 0x1Fu;
    if (!(zf = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    fseek(zf, 0, SEEK_END);
    zsize = (uint32_t)ftell(zf);
    dlog_init();
    if (zip_open(&zip, 3, SCRATCH, SCRATCH_MAX) != ZIP_OK || !s2gfx_init(&zip) || !s2plates_load(&zip)) { fprintf(stderr, "zip / toolkit / plates\n"); return 1; }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    run_model();
    const int states_ok = part_states(argv[2], 0) & part_states(argv[2], 1);
    plates_on = 1;
    if (!picture("LEAG_1L.BKK")) { fprintf(stderr, "LEAG_1L.BKK\n"); return 1; }
    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;

    static char names[64][64];
    static char labels[64][48];
    static struct cand cands[64][40];
    static int ncand[64], hund[64][40];
    int nn = 0;
    FILE *sf = fopen(argv[3], "r");
    char name[64], label[48];
    while (sf) {
        struct cand c;
        int h;
        if (fscanf(sf, "%63s %47s %d", name, label, &h) != 3) break;
        int ok = 1;
        for (int k = 0; k < 28; k++) ok = ok && fscanf(sf, "%d", &c.v[k]) == 1;
        if (!ok) break;
        int k;
        for (k = 0; k < nn; k++) if (!strcmp(names[k], name)) break;
        if (k == nn) {
            if (nn == 64) continue;
            snprintf(names[nn], 64, "%s", name);
            snprintf(labels[nn], 48, "%s", label);
            nn++;
        }
        if (ncand[k] < 40) { hund[k][ncand[k]] = h; cands[k][ncand[k]++] = c; }
    }
    if (sf) fclose(sf);

    int frames = 0, sum_diff = 0, sum_both = 0, sum_equal = 0, sum_ref = 0, sum_mine = 0, sum_ref_ok = 0, sum_mine_ok = 0, sum_pieces = 0, sum_faces = 0;
    for (int f = 0; f < nn; f++) {
        if (only && !strstr(names[f], only) && !strstr(labels[f], only)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s.idx", argv[4], names[f]);
        FILE *h = fopen(path, "rb");
        if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%s.pal", argv[4], names[f]);
        h = fopen(path, "rb");
        if (!h || fread(shot_pal, 4, 256, h) != 256) memset(shot_pal, 0, sizeof shot_pal);
        if (h) fclose(h);
        if (!same_palette(shot_pal, pal)) { printf("%s (%s): palette is not the picture's (a fade)\n", names[f], labels[f]); continue; }
        int best = 1 << 30, bk = 0;
        for (int k = 0; k < ncand[f]; k++) {
            render(cands[f][k].v);
            const int d = diff();
            if (d < best) { best = d; bk = k; }
        }
        render(cands[f][bk].v);
        const unsigned pieces = pieces_drawn, faces = faces_drawn;
        /* the plates' pixels: below the band (rows 42 on), where the screenshot or ours is not the plain picture */
        const volatile uint8_t *fb = FB(0);
        const uint8_t *pic = s2gfx_pic(0);
        int ref_n = 0, mine_n = 0, both = 0, equal = 0, ref_ok = 0, mine_ok = 0;
        for (int y = 42; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                const int i = y * 320 + x;
                const int r = frame_idx[i] != pic[i], m = fb[i] != pic[i];
                if (!r && !m) continue;
                if (r) ref_n++;
                if (m) mine_n++;
                if (r && m) {
                    both++;
                    equal += fb[i] == frame_idx[i];
                }
                int near_r = 0, near_m = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (x + dx < 0 || x + dx > 319 || y + dy < 42 || y + dy > 199) continue;
                        const int j = i + dy * 320 + dx;
                        near_r |= fb[j] == frame_idx[i];
                        near_m |= frame_idx[j] == fb[i];
                    }
                if (r) ref_ok += near_r;
                if (m) mine_ok += near_m;
            }
        printf("%s (%s): hundredths %d, %.2f %% of the pixels differ; the plates' pixels: the screenshot's %d, ours %d, both %d of which %d equal; of the screenshot's %d within a pixel of ours, "
               "of ours %d within a pixel of the screenshot's; %u pieces, %u faces\n", names[f], labels[f], hund[f][bk], 100.0 * best / S2G_PIC_BYTES, ref_n, mine_n, both, equal, ref_ok, mine_ok, pieces, faces);
        frames++;
        sum_diff += best;
        sum_both += both;
        sum_equal += equal;
        sum_ref += ref_n;
        sum_mine += mine_n;
        sum_ref_ok += ref_ok;
        sum_mine_ok += mine_ok;
        sum_pieces += (int)pieces;
        sum_faces += (int)faces;
        if (dump_name && argc > 5 && (strstr(names[f], dump_name) || strstr(labels[f], dump_name))) {
            dump(argv[5], names[f], "mine");
            dump(argv[5], names[f], "ref");
            dump(argv[5], names[f], "diff");
            snprintf(path, sizeof path, "%s_%s_mine.idx", argv[5], names[f]);
            FILE *o = fopen(path, "wb");
            if (o) { fwrite((const void *)FB(0), 1, S2G_PIC_BYTES, o); fclose(o); }
        }
    }
    printf("%d screenshots: %.2f %% of the pixels differ; the plates' pixels: the screenshots' %d, ours %d, both %d of which %d equal (%.1f %%); of the screenshots' %.1f %% within a pixel of ours, "
           "of ours %.1f %% within a pixel of the screenshots'; %d pieces, %d faces handed to the geometry unit a frame\n", frames, frames ? 100.0 * sum_diff / ((double)frames * S2G_PIC_BYTES) : 0.0,
           sum_ref, sum_mine, sum_both, sum_equal, sum_both ? 100.0 * sum_equal / sum_both : 0.0, sum_ref ? 100.0 * sum_ref_ok / sum_ref : 0.0, sum_mine ? 100.0 * sum_mine_ok / sum_mine : 0.0,
           frames ? sum_pieces / frames : 0, frames ? sum_faces / frames : 0);
    printf("%s\n", states_ok ? "the states: all equal" : "the states: DIFFER");
    return states_ok ? 0 : 1;
}
