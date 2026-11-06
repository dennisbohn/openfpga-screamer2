/*
 * The championship's desk scene (firmware/s2desk.c) against the running original: tools/desk.sh gives this the recording of `python -I tools/s2/dosbox_desk.py report` (or `desk`)
 * (tools/desk_shots.py: a row for every change of the camera record 0x131ea4, the car's object record, the textures' clock, the wheels' phase and the page's flags) and its raw
 * screenshots (320 x 200 palette indices, tools/pages3d_idx.py). Two parts for each scene in the recording:
 *
 *  1. The states. The scene is read from the ZIP as the page reads it and the firmware's clock is run from the first frame (s2desk_step(0), 1, 2 ..); every recorded row is looked
 *     for among those frames: the same camera place and angles, textures' clock, car place and angles, wheels' phase. A row whose numbers are not all of one frame is one the poll
 *     read while the page was writing them: it is "torn" if each number is that of the frame f or the next, else it is wrong. The frame in which the fade's flag (0x132498) is first
 *     set is compared with the frame in which the firmware's key() would start the fade (the scene's time reaching 0.98).
 *  2. The pictures. For each screenshot the states round the moment of its request (every distinct row in a window of 0.16 s) are drawn as the firmware's page draws them (the backdrop,
 *     the team's room, the car as an object sorted in among its pieces, with its own material file) and compared pixel for pixel with the screenshot; the best state is its frame.
 *     Where the screenshot's picture or ours differs from the other's in more than the textures' sampling (the original fills textures affine, the unit is perspective-correct) the
 *     numbers say so: pixels equal, pixels of a colour ramp that is not the other's, edges of the colour ramps found within a pixel of each other's.
 *
 * Usage: desk_match <SCREAMER2.zip> <rows file> <shots file> <folder of .idx> [<out prefix: pictures of the best match per screenshot (DUMP=<part of a name>)>]
 *        TEAM=<1..4> (default 1), CAR=<1..5> (default 2: the private CHOICE.DAT of the run has the team's second car; scene 4 shows the fifth)
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
void s2mm_song(void) {}
int s2page_run(const struct s2_page *p, void *self) { (void)p; (void)self; return 0; }
struct viewer_image loaded;
struct zip *files_zip(void) { return &zip; }
void s2room_forget(void) {}

#include "../s2desk.c"                                          /* (included: the test reaches the page's drawing) */

static const struct s2room_mem mem = { (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000), 0x400000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000), 0x700000, 0, 0 };

static uint8_t frame_idx[S2G_PIC_BYTES];
static uint32_t shot_pal[256];

/* ------------------------------------------------------------------------------------------------ the rows */

#define MAX_FRAMES 900
struct dstate { int32_t v[14]; };                               /* camera x y z a0 a1 a2, textures' clock, car x y z a0 a1 a2, wheels' phase (the float's bits) */
static struct dstate model[MAX_FRAMES];
static int model_frames, model_fade = -1;
static int32_t model_neg[MAX_FRAMES];                   /* the wheels' phase as the original stores it first, before it adds 2 pi to a negative one (a poll can read it): its bits, 0 if there was none */

struct rrow { int fade, level, hund, scene; int32_t v[14]; int league; };
#define WILD INT32_MIN                                  /* a number the recording does not have (the car's record when it was not read) */
static int same(const int32_t *a, const int32_t *b)
{
    for (int i = 0; i < 14; i++)
        if (b[i] != WILD && a[i] != b[i]) return 0;
    return 1;
}

static void run_model(void)
{
    struct s2desk_state st;
    model_frames = 0;
    model_fade = -1;
    for (int f = 0; f < MAX_FRAMES; f++) {
        s2desk_step(f, &st);
        uint32_t wb;
        memcpy(&wb, &st.wheels, 4);
        const int32_t anim = (int32_t)((double)st.anim * 65535.0);
        model[f] = (struct dstate){ { st.cam[0], st.cam[1], st.cam[2], st.a0, st.a1, 0, anim, st.car[0], st.car[1], st.car[2], 0, st.car_a1, 0, (int32_t)wb } };
        model_neg[f] = 0;
        if (f > 0) {
            float before;
            memcpy(&before, &model[f - 1].v[13], 4);
            const float neg = (float)((double)before - (double)DT * (double)TWO_PI_F * WHEEL_RATE);
            if (neg < 0.0f) memcpy(&model_neg[f], &neg, 4);
        }
        model_frames = f + 1;
        if (model_fade < 0 && (double)t_next >= SKIP_AT) model_fade = f;
        if (model_fade >= 0 && f > model_fade + 40) break;
    }
}

/* perturb: the negative control, every 50th row's camera x moved by one: the test must find them wrong */
static int part_states(const struct rrow *rows_in, int n, int scene, int perturb)
{
    static struct rrow rows[20000];
    memcpy(rows, rows_in, sizeof(struct rrow) * (size_t)n);
    if (perturb)
        for (int k = 0; k < n; k += 50) rows[k].v[0]++;
    int exact = 0, torn = 0, wrong = 0, first_wrong = -1, before = -1, flagged = -1, used = 0, started = 0;   /* before: the last frame seen with the fade's flag clear, flagged: the first with it set */
    for (int k = 0; k < n; k++) {
        const struct rrow *r = &rows[k];
        if (r->scene != scene) continue;
        int found = -1;
        for (int fr = 0; fr < model_frames && found < 0; fr++)
            if (same(model[fr].v, r->v)) found = fr;
        if (!started && found < 0) continue;            /* (the rows before the first frame: the scene is loaded, the camera is still the page before's) */
        started = 1;
        used++;
        if (found >= 0) {
            exact++;
            if (r->fade && flagged < 0) flagged = found;
            else if (!r->fade && flagged < 0 && found > before) before = found;
            continue;
        }
        int tornrow = 0;
        for (int fr = 0; fr + 1 < model_frames && !tornrow; fr++) {
            int all = 1;
            for (int i = 0; i < 14 && all; i++)
                all = r->v[i] == WILD || r->v[i] == model[fr].v[i] || r->v[i] == model[fr + 1].v[i] || (i == 13 && model_neg[fr + 1] && r->v[i] == model_neg[fr + 1]) ||
                      (i == 13 && model_neg[fr] && r->v[i] == model_neg[fr]);
            tornrow = all;
        }
        if (tornrow) {
            torn++;
            for (int fr = 0; fr + 1 < model_frames && flagged < 0; fr++) {
                int all = 1;
                for (int i = 0; i < 14 && all; i++) all = r->v[i] == WILD || r->v[i] == model[fr].v[i] || r->v[i] == model[fr + 1].v[i];
                if (!all) continue;
                if (r->fade) flagged = fr + 1;
                else if (fr > before) before = fr;
                break;
            }
        } else {
            wrong++;
            if (first_wrong < 0) first_wrong = k;
        }
    }
    if (perturb) {
        printf("scene %d negative control (every 50th row's camera x moved by one): %d rows wrong: %s\n", scene, wrong, wrong ? "found" : "NOT FOUND");
        return wrong > 0;
    }
    printf("scene %d states: %d rows: %d are one frame of the firmware's scene exactly, %d are rows the poll read while the page was writing them, %d are wrong\n", scene, used, exact, torn, wrong);
    if (first_wrong >= 0) {
        const struct rrow *r = &rows[first_wrong];
        printf("  the first wrong row is %d (hundredths %d):", first_wrong, r->hund);
        for (int i = 0; i < 14; i++) printf(" %d", r->v[i]);
        printf("\n  the nearest frame:");
        int best = -1, bd = 1 << 30;
        for (int fr = 0; fr < model_frames; fr++) {
            int d = 0;
            for (int i = 0; i < 14; i++) d += r->v[i] != WILD && model[fr].v[i] != r->v[i];
            if (d < bd) { bd = d; best = fr; }
        }
        for (int i = 0; i < 14; i++) printf(" %d", model[best].v[i]);
        printf(" (frame %d, %d numbers differ)\n", best, bd);
    }
    const int inside = before <= model_fade && model_fade <= flagged;
    printf("  the firmware starts the fade in frame %d (the scene's time reaches 0.98: %.3f s of the scene at 50 frames a second); the original's flag is clear in the rows up to frame %d and set from "
           "frame %d on (the poll misses a few frames while a screenshot is taken)%s\n", model_fade, model_fade * 0.02, before, flagged, inside ? ": the firmware's frame is between them" : ": NOT between them");
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

/* of the pixels that differ, how many have the screenshot's colour within a pixel of ours */
static int near_count(void)
{
    const volatile uint8_t *fb = FB(0);
    int near = 0;
    for (int y = 1; y < 199; y++)
        for (int x = 1; x < 319; x++) {
            const int i = y * 320 + x;
            if (fb[i] == frame_idx[i]) continue;
            int found = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (fb[i + dy * 320 + dx] == frame_idx[i]) found = 1;
            near += found;
        }
    return near;
}

static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

struct cand { int32_t v[12]; };                                 /* scene, camera x y z a0 a1, clock, car x y z a1 ... then the wheels' bits */

static void render(const int32_t *v, int32_t wheel_bits)
{
    memset(&D, 0, sizeof D);
    D.fresh = 1;
    D.st.cam[0] = v[1];
    D.st.cam[1] = v[2];
    D.st.cam[2] = v[3];
    D.st.a0 = v[4];
    D.st.a1 = v[5];
    const int wild = v[7] == WILD;                              /* (a recording without the car's record: the car is where the scene's paths put it, which a car that stands does at every frame) */
    for (int k = 0; k < 3; k++) D.st.car[k] = wild ? model[0].v[7 + k] : v[7 + k];
    D.st.car_a1 = wild ? model[0].v[11] : v[10];
    memcpy(&D.st.wheels, &wheel_bits, 4);
    track_clock_set(((float)v[6] + 0.5f) / 65535.0f);
    memset(&refl, 0, sizeof refl);
    struct blit_list *l = s2gfx_begin_on(0);
    pieces_drawn = faces_drawn = 0;
    desk_draw(&D, l, 0);
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

static int is_edge(const uint8_t *p, int x, int y)
{
    return x < 319 && y < 199 && ((p[y * 320 + x] >> 4) != (p[y * 320 + x + 1] >> 4) || (p[y * 320 + x] >> 4) != (p[(y + 1) * 320 + x] >> 4));
}

static int edge_near(const uint8_t *p, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (x + dx >= 0 && x + dx < 320 && y + dy >= 0 && y + dy < 200 && is_edge(p, x + dx, y + dy)) return 1;
    return 0;
}

/* what a frame of the scene hands the geometry unit, every 5th frame of the firmware's scene from the first to the end of the fade (the counts of the CPU's and the unit's work):
   the pieces and the faces on average and at most (the model has been run: run_model()) */
static void cost(int sc, unsigned out[4])
{
    unsigned sp = 0, sf = 0, mp = 0, mf = 0, cnt = 0;
    for (int fr = 0; fr < model_frames && fr <= model_fade + 25; fr += 5) {
        const int32_t *mv = model[fr].v;
        const int32_t cv[12] = { sc, mv[0], mv[1], mv[2], mv[3], mv[4], mv[6], mv[7], mv[8], mv[9], mv[11], mv[13] };
        render(cv, mv[13]);
        sp += pieces_drawn;
        sf += faces_drawn;
        if (pieces_drawn > mp) mp = pieces_drawn;
        if (faces_drawn > mf) mf = faces_drawn;
        cnt++;
    }
    out[0] = cnt ? sp / cnt : 0;
    out[1] = cnt ? sf / cnt : 0;
    out[2] = mp;
    out[3] = mf;
}

/* ALL=1: every scene of every team with every car of the team (4 x 4 x 5): the compact container the firmware reads (desk_map) builds the very image the whole SCENE<nn>1.DAT does
   (the builder's output byte for byte, 16 times), the paths of the file are usable, and each car of the team's SCCAR file loads with its windows. Prints the bytes the firmware reads. */
static int all_scenes(const struct s2room_mem *m)
{
    static uint8_t img2[0x700000];
    static uint8_t *whole;
    if (!whole) whole = malloc(0x600000);
    int bad = 0;
    uint32_t read_min = ~0u, read_max = 0;
    unsigned cost_mean_max = 0, cost_max = 0, pieces_mean_max = 0, pieces_max = 0;
    for (int sc = 1; sc <= S2DESK_SCENES; sc++) {
        char path[24] = "ANIMS/SCENE011.DAT";
        path[12] = (char)('0' + sc);
        const struct zip_entry *e = zip_find(&zip, path);
        if (!e || e->size > 0x600000 || zip_read(&zip, e, whole, 0x600000)) { printf("%s: not readable\n", path); return 0; }
        for (int team = 1; team <= 4; team++) {
            const int loaded_ok = desk_load(&zip, m, sc, team, 1);
            const uint32_t size = loaded_ok ? (uint32_t)rd32(m->image + 0x88) : 0;
            struct track_build_info info;
            const struct s2_files f = { .gfx = whole, .gfx_size = e->size };
            const uint32_t size2 = track_build_s2(&f, team, img2, sizeof img2, m->scratch, m->scratch_max, &info);
            const int same_image = loaded_ok && size == size2 && !memcmp(m->image, img2, size);
            int cars_ok = 1;
            for (int car = 1; car <= 5; car++) cars_ok &= desk_car(&zip, team, car);
            unsigned c[4] = { 0, 0, 0, 0 };
            if (loaded_ok && cars_ok && desk_car(&zip, team, 1)) {
                run_model();
                cost(sc, c);
            }
            if (c[1] > cost_mean_max) cost_mean_max = c[1];
            if (c[3] > cost_max) cost_max = c[3];
            if (c[0] > pieces_mean_max) pieces_mean_max = c[0];
            if (c[2] > pieces_max) pieces_max = c[2];
            /* the bytes this scene reads: the five entries as stored (desk_map), the paths and ENV.MLB, the directory's last 4 KB, the car's .MLB and .3DX and its directory */
            uint32_t reads = 0;
            struct dat_entry d;
            static const char *const ext[5] = { "MMP", "UNR", "COL", "MLB", "BCK" };
            for (int k = 0; k < 5; k++) {
                char name[16] = "MAP1.MMP";
                name[3] = (char)('0' + team);
                memcpy(name + 5, ext[k], 3);
                if (dat_find(scene_c.dir, scene_c.dir_len, name, &d)) reads += d.stored;
            }
            reads += 4096 + 4096;
            if (dat_find(scene_c.dir, scene_c.dir_len, "ENV.MLB", &d)) reads += d.stored;
            if (dat_find(scene_c.dir, scene_c.dir_len, "SPLINES.PTH", &d)) reads += d.stored;
            if (dat_find(scene_c.dir, scene_c.dir_len, "SPLINES.TXT", &d)) reads += d.stored;
            printf("scene %d team %d: image %u bytes %s the whole file's, the five cars %s, %u of the file's %u bytes read (without the car's); a frame hands the unit %u pieces, %u faces (%u, %u at most)\n", sc,
                   team, size, same_image ? "=" : "DIFFERS FROM", cars_ok ? "load" : "FAIL", reads, e->size, c[0], c[1], c[2], c[3]);
            if (!same_image || !cars_ok) bad++;
            if (reads < read_min) read_min = reads;
            if (reads > read_max) read_max = reads;
        }
    }
    printf("all 16 scenes of the 4 teams: %d wrong; %u to %u bytes read (without the car's, 0.05 MB); at most %u pieces and %u faces a frame on average of a scene, %u and %u in any sampled frame\n", bad,
           read_min, read_max, pieces_mean_max, cost_mean_max, pieces_max, cost_max);
    return bad == 0;
}

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
    if (zip_open(&zip, 3, SCRATCH, SCRATCH_MAX) != ZIP_OK || !s2gfx_init(&zip)) { fprintf(stderr, "zip / toolkit\n"); return 1; }
    struct s2room_mem m = mem;
    m.scratch = SCRATCH;
    m.scratch_max = SCRATCH_MAX;
    if (getenv("ALL") && *getenv("ALL")) return all_scenes(&m) ? 0 : 1;
    const int team = getenv("TEAM") && atoi(getenv("TEAM")) >= 1 && atoi(getenv("TEAM")) <= 4 ? atoi(getenv("TEAM")) : 1;
    const int car_env = getenv("CAR") && atoi(getenv("CAR")) >= 1 && atoi(getenv("CAR")) <= 5 ? atoi(getenv("CAR")) : 2;
    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;

    static struct rrow rows[20000];
    int n = 0;
    FILE *f = fopen(argv[2], "r");
    if (!f) { perror(argv[2]); return 1; }
    char line[512];
    while (n < 20000 && fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        struct rrow *r = &rows[n];
        int objs, paths, dummy;
        if (sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &r->fade, &r->level, &r->hund, &r->scene, &r->v[0], &r->v[1], &r->v[2], &r->v[3], &r->v[4], &r->v[5], &r->v[6],
                   &r->v[7], &r->v[8], &r->v[9], &r->v[10], &r->v[11], &r->v[12], &r->v[13], &r->league, &objs) < 20) continue;
        (void)paths;
        (void)dummy;
        n++;
    }
    fclose(f);

    static char names[400][64];
    static char labels[400][48];
    static struct cand cands[400][40];
    static int wheelbits[400][40], ncand[400], scenes[400];
    int nn = 0;
    FILE *sf = fopen(argv[3], "r");
    char name[64], label[48];
    while (sf) {
        struct cand c;
        int h, wb;
        if (fscanf(sf, "%63s %47s %d", name, label, &h) != 3) break;
        int ok = 1;
        for (int k = 0; k < 12; k++) ok = ok && fscanf(sf, "%d", &c.v[k]) == 1;
        if (!ok) break;
        wb = c.v[11];
        int k;
        for (k = 0; k < nn; k++) if (!strcmp(names[k], name)) break;
        if (k == nn) {
            if (nn == 400) continue;
            snprintf(names[nn], 64, "%s", name);
            snprintf(labels[nn], 48, "%s", label);
            scenes[nn] = c.v[0];
            nn++;
        }
        if (ncand[k] < 40) { wheelbits[k][ncand[k]] = wb; cands[k][ncand[k]++] = c; }
    }
    if (sf) fclose(sf);

    int states_ok = 1, frames = 0, sum_near = 0, sum_diff = 0, sum_edge_ref = 0, sum_edge_ref_ok = 0, sum_edge_mine = 0, sum_edge_mine_ok = 0, sum_pieces = 0, sum_faces = 0, seen_scene[5] = { 0 };
    for (int sc = 1; sc <= S2DESK_SCENES; sc++) {
        int present = 0;
        for (int k = 0; k < n; k++) present += rows[k].scene == sc;
        for (int k = 0; k < nn; k++) present += scenes[k] == sc;
        if (!present || seen_scene[sc]) continue;
        seen_scene[sc] = 1;
        const int car = sc == 4 ? 5 : car_env;
        const int loaded_ok = desk_load(&zip, &m, sc, team, car);
        printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
        if (!loaded_ok) { fprintf(stderr, "scene %d: could not be loaded\n", sc); return 1; }
        run_model();
        printf("scene %d (SPLINES.TXT: %.1f s, textures' period %.1f s, %d cameras)\n", sc, (double)seconds, (double)period, ncam);
        if (!part_states(rows, n, sc, 0) || !part_states(rows, n, sc, 1)) states_ok = 0;
        {
            unsigned c[4];
            cost(sc, c);
            printf("scene %d: the geometry unit is handed %u pieces and %u faces a frame on average, %u and %u at most (every 5th frame of the scene)\n", sc, c[0], c[1], c[2], c[3]);
        }
        for (int fi = 0; fi < nn; fi++) {
            if (scenes[fi] != sc) continue;
            if (only && !strstr(names[fi], only) && !strstr(labels[fi], only)) continue;
            char path[512];
            snprintf(path, sizeof path, "%s/%s.idx", argv[4], names[fi]);
            FILE *h = fopen(path, "rb");
            if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
            fclose(h);
            snprintf(path, sizeof path, "%s/%s.pal", argv[4], names[fi]);
            h = fopen(path, "rb");
            if (!h || fread(shot_pal, 4, 256, h) != 256) memset(shot_pal, 0, sizeof shot_pal);
            if (h) fclose(h);
            if (!same_palette(shot_pal, track_palette())) { printf("%s (%s): palette is not the scene's (a fade)\n", names[fi], labels[fi]); continue; }
            int best = 1 << 30, bk = 0;
            for (int k = 0; k < ncand[fi]; k++) {
                render(cands[fi][k].v, wheelbits[fi][k]);
                const int d = diff();
                if (d < best) { best = d; bk = k; }
            }
            render(cands[fi][bk].v, wheelbits[fi][bk]);
            const unsigned pieces = pieces_drawn, faces = faces_drawn;
            static uint8_t mine[S2G_PIC_BYTES];
            memcpy(mine, (const void *)FB(0), S2G_PIC_BYTES);
            int ref_n = 0, ref_ok = 0, my_n = 0, my_ok = 0;
            for (int y = 0; y < 200; y++)
                for (int x = 0; x < 320; x++) {
                    if (is_edge(frame_idx, x, y)) { ref_n++; ref_ok += edge_near(mine, x, y); }
                    if (is_edge(mine, x, y)) { my_n++; my_ok += edge_near(frame_idx, x, y); }
                }
            const int near = near_count();
            printf("%s (%s): scene %d: %.1f %% of the pixels differ, %.1f %% of those within a pixel's reach; edges: %.1f %% of the screenshot's have one of ours within a pixel, %.1f %% of ours one of "
                   "the screenshot's; %u pieces, %u faces\n", names[fi], labels[fi], sc, 100.0 * best / S2G_PIC_BYTES, best ? 100.0 * near / best : 0.0, ref_n ? 100.0 * ref_ok / ref_n : 0.0,
                   my_n ? 100.0 * my_ok / my_n : 0.0, pieces, faces);
            sum_near += near;
            frames++;
            sum_diff += best;
            sum_edge_ref += ref_n;
            sum_edge_ref_ok += ref_ok;
            sum_edge_mine += my_n;
            sum_edge_mine_ok += my_ok;
            sum_pieces += (int)pieces;
            sum_faces += (int)faces;
            if (dump_name && argc > 5 && (strstr(names[fi], dump_name) || strstr(labels[fi], dump_name))) {
                dump(argv[5], names[fi], "mine");
                dump(argv[5], names[fi], "ref");
                dump(argv[5], names[fi], "diff");
                snprintf(path, sizeof path, "%s_%s_mine.idx", argv[5], names[fi]);
                FILE *o = fopen(path, "wb");
                if (o) { fwrite(mine, 1, S2G_PIC_BYTES, o); fclose(o); }
            }
            fflush(stdout);
        }
    }
    printf("%d screenshots: %.1f %% of the pixels differ, %.1f %% of those within a pixel of the screenshot's colour; edges: %.1f %% of the screenshots' have one of ours within a pixel, %.1f %% of ours one of theirs; %d pieces, %d faces handed to the "
           "geometry unit a frame\n", frames, frames ? 100.0 * sum_diff / ((double)frames * S2G_PIC_BYTES) : 0.0, sum_diff ? 100.0 * sum_near / sum_diff : 0.0, sum_edge_ref ? 100.0 * sum_edge_ref_ok / sum_edge_ref : 0.0,
           sum_edge_mine ? 100.0 * sum_edge_mine_ok / sum_edge_mine : 0.0, frames ? sum_pieces / frames : 0, frames ? sum_faces / frames : 0);
    printf("%s\n", states_ok ? "the states: all equal" : "the states: DIFFER");
    return states_ok ? 0 : 1;
}
