/*
 * The ARCADE title's cars (firmware/s2arcscene.c, the title of s2arcade.c) against the running original: tools/scene.sh gives this the recording of tools/s2/dosbox_scene.py
 * (tools/scene_shots.py: every change of the original's camera record and four cars' object records, one row each) and its raw screenshots (320 x 200 palette indices,
 * tools/pages3d_idx.py). Two parts:
 *
 *  1. The states. The firmware's clock is run from the first frame (s2arcscene_frame(0), 1, 2 ..) over four takes; every recorded row is looked for among those frames: the same
 *     place (whole units), heading and angles of each of the four cars and the same camera record. A row whose numbers are not all one frame's is one the poll read while the
 *     game was writing it (it writes a number at a time): it is "torn" if each number is that of the frame f or the next, else it is wrong. The take's length (frames, the
 *     hundredths of the original's own clock between two takes) is read off the rows too.
 *  2. The pictures. For each screenshot the frames round the moment of its request (the original's hundredths at the last row before it say which frame that was: 16 frames before to
 *     4 after, the poll and the screen are not in step) are drawn as the firmware's title draws them, every frame from the first (the picture, the cars from the camera, the prompt as
 *     the hundredths have it; the windows' reflection builds up from frame to frame), and the one whose silhouette of cars (the pixels that are not the plain title picture) misses
 *     the screenshot's least is its frame. The two pictures are then compared pixel for pixel. Where the original's picture or ours differs from the plain title picture (the cars'
 *     pixels, either side) it counts how many are equal, and how many of the rest have the other's colour within a pixel (the original fills textures affine, the unit is
 *     perspective-correct, so the cars' textures are not the same pixels; their places and shapes are).
 *
 * Usage: scene_match <SCREAMER2.zip> <rows file> <folder of .idx> [<out prefix: pictures of the best match per screenshot (DUMP=<part of a name>)>]
 */
#define _GNU_SOURCE
#include "../hw.h"
#include "../s2gfx.h"
#include "../s2menu.h"
#include "../s2page.h"
#include "../s2save.h"
#include "../s2arcscene.h"
#include "../s2showroom.h"
#include "../slot.h"
#include "../dlog.h"
#include "../dat.h"
#include "../s2scene.h"
#include "../setup.h"
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
void s2tp_modes_draw(struct blit_list *l, int free_run, int lit_phase) { (void)l; (void)free_run; (void)lit_phase; }
struct viewer_image loaded;
struct zip *files_zip(void) { return &zip; }

#define track_draw arcade_track_draw                           /* (the TRACK page's drawing is not the engine's track_draw() of track.h, which the geometry calls need) */
#include "../s2arcade.c"                                        /* (included: the test reaches the title's own drawing) */
#undef track_draw

#define TAKE_FRAMES 201
#define TICKS       (4 * TAKE_FRAMES + 8)

struct state { int32_t car[4][3]; int a1[4], take; int32_t cam[3]; int a0, ca1; };
static struct state states[TICKS];
struct row { double t; int hund, a0, a1, a2, cam[3], car[4][3], ca0[4], ca1[4], ca2[4]; };
static struct row rows[20000];
static int nrows;
static uint8_t frame_idx[S2G_PIC_BYTES], plain[S2G_PIC_BYTES];
static uint32_t shot_pal[256], arcade_pal[256];

static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

/* the firmware's clock from the first frame: the states of every frame, the page's tick being the frame's number */
static void run_states(void)
{
    for (int f = 0; f < TICKS; f++) {
        s2arcscene_frame((uint32_t)f);
        struct state *s = &states[f];
        for (int i = 0; i < 4; i++) s2arcscene_car(i, s->car[i], &s->a1[i]);
        s2arcscene_camera(s->cam, &s->a0, &s->ca1);
        s->take = s2arcscene_take();
    }
}

static int control_heading;                                 /* the negative control: the model's headings turned by this many 2048ths */

static int car_is(const struct row *r, int i, const struct state *s)
{
    return r->car[i][0] == s->car[i][0] && r->car[i][1] == s->car[i][1] && r->car[i][2] == s->car[i][2] && r->ca1[i] == ((s->a1[i] + control_heading) & 0x7FF) && !r->ca0[i] && !r->ca2[i];
}

static int cam_is(const struct row *r, const struct state *s)
{
    return r->cam[0] == s->cam[0] && r->cam[1] == s->cam[1] && r->cam[2] == s->cam[2] && r->a0 == s->a0 && r->a1 == s->ca1 && !r->a2;
}

/* the first frame whose state is the row's (4 cars and the camera), -1 if none; or, torn: every number is the frame's or the next one's (the second result) */
static int match_row(const struct row *r, int *torn)
{
    *torn = -1;
    for (int f = 0; f + 1 < TICKS; f++) {
        if (!cam_is(r, &states[f])) continue;
        int all = 1, mixed = 1;
        for (int i = 0; i < 4; i++) {
            all &= car_is(r, i, &states[f]);
            for (int k = 0; k < 3; k++) mixed &= r->car[i][k] == states[f].car[i][k] || r->car[i][k] == states[f + 1].car[i][k];
            mixed &= r->ca1[i] == ((states[f].a1[i] + control_heading) & 0x7FF) || r->ca1[i] == ((states[f + 1].a1[i] + control_heading) & 0x7FF);
            mixed &= !r->ca0[i] && !r->ca2[i];
        }
        if (all) return f;
        if (mixed && *torn < 0) *torn = f;
    }
    return -1;
}

static int diff(void)
{
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int i = 0; i < S2G_PIC_BYTES; i++) d += fb[i] != frame_idx[i];
    return d;
}

/* the silhouettes: the pixels that are not the plain picture's (the prompt is the same on both sides where it is drawn: the plain picture has none, so it counts on both). The pixels in
   one of the two and not in the other: where the cars stand, whatever their textures show (the texture filling is not the same: affine, perspective-correct) */
static int silhouette_miss(void)
{
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int i = 0; i < S2G_PIC_BYTES; i++) d += (fb[i] != plain[i]) != (frame_idx[i] != plain[i]);
    return d;
}

/* the title as the firmware draws it at the page's frame f (the clock run from 0, every frame drawn: the windows' reflection builds up from frame to frame): the picture, the cars, the
   prompt as the original's hundredths `blink` have it (the page's own clock is 2 a frame from 0, the original's runs on from the program's start) */
static int made = -1;                                       /* the frame the page has been run to */
static void render(int f, int blink)
{
    struct title_page tt = { 1 };
    if (made < 0 || f < made) {
        made = -1;
        s2showroom_init(S2ARCSCENE_CARS, 0);
    }
    for (int i = made + 1; i <= f; i++) {
        struct blit_list *l = s2gfx_begin_on(0);
        title_frame(&tt, (uint32_t)i);
        pieces_drawn = faces_drawn = 0;
        s2gfx_picture(l, s2gfx_pic(0));
        title_draw(&tt, l, (uint32_t)(i == f ? blink : i));
        s2gfx_flush();
    }
    made = f;
}

static void dump(const char *prefix, const char *name, const char *what, int diffmap)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s_%s.rgb", prefix, name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (diffmap) c = fb[i] != frame_idx[i] ? 0xFFFFFF : 0;
        else c = shot_pal[strcmp(what, "ref") ? fb[i] : frame_idx[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <rows file> <folder of .idx> [out prefix]\n", argv[0]); return 2; }
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
    const int team = getenv("TEAM") && atoi(getenv("TEAM")) >= 1 && atoi(getenv("TEAM")) <= 4 ? atoi(getenv("TEAM")) : 1;      /* TEAM=<1 .. 4>: the team whose cars the run shows */
    if (!s2arcscene_load(&zip, team, 5)) { fprintf(stderr, "the paths or the cars\n"); return 1; }
    s2showroom_init(S2ARCSCENE_CARS, 0);
    if (!s2gfx_bkk("ARCADE_L.BKK", s2gfx_pic(0), arcade_pal)) { fprintf(stderr, "ARCADE_L.BKK\n"); return 1; }
    memcpy(plain, s2gfx_pic(0), S2G_PIC_BYTES);
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    const char *dump_name = getenv("DUMP");
    if (dump_name && !*dump_name) dump_name = 0;

    run_states();
    FILE *rf = fopen(argv[2], "r");
    if (!rf) { perror(argv[2]); return 1; }
    static struct { char name[64], label[48]; int at; } shots[200];
    int nshots = 0;
    char kind[4];
    while (fscanf(rf, "%3s", kind) == 1) {
        if (kind[0] == 'R') {
            struct row *r = &rows[nrows];
            int idx;
            if (nrows >= 20000 || fscanf(rf, "%d %lf %d %d %d %d %d %d %d", &idx, &r->t, &r->hund, &r->a0, &r->a1, &r->a2, &r->cam[0], &r->cam[1], &r->cam[2]) != 9) break;
            for (int i = 0; i < 4; i++)
                if (fscanf(rf, "%d %d %d %d %d %d", &r->car[i][0], &r->car[i][1], &r->car[i][2], &r->ca0[i], &r->ca1[i], &r->ca2[i]) != 6) return 1;
            nrows++;
        } else if (nshots < 200) {
            if (fscanf(rf, "%63s %47s %d", shots[nshots].name, shots[nshots].label, &shots[nshots].at) != 3) break;
            nshots++;
        }
    }
    fclose(rf);

    /* 1. the states */
    int equal = 0, torn = 0, wrong = 0, first_wrong = -1;
    static int frame_of[20000];
    for (int k = 0; k < nrows; k++) {
        int t;
        const int f = match_row(&rows[k], &t);
        frame_of[k] = f >= 0 ? f : t;
        if (f >= 0) equal++;
        else if (t >= 0) torn++;
        else {
            wrong++;
            if (first_wrong < 0) first_wrong = k;
        }
    }
    printf("states: %d recorded rows: %d are a frame of the firmware's model exactly (4 cars: place, heading, no pitch or roll; the camera record), %d are torn (each number is that of two "
           "neighbouring frames: the poll read the game's memory while it wrote), %d are neither\n", nrows, equal, torn, wrong);
    if (wrong) {
        const struct row *r = &rows[first_wrong];
        printf("  first: row %d: car 0 (%d, %d, %d) a1 %d, camera (%d, %d, %d) a0 %d a1 %d\n", first_wrong, r->car[0][0], r->car[0][1], r->car[0][2], r->ca1[0], r->cam[0], r->cam[1], r->cam[2], r->a0, r->a1);
    }
    /* the negative control: the model's headings one 2048th off must find none of the rows (else the comparison cannot tell a heading) */
    control_heading = 1;
    int control_hits = 0;
    for (int k = 0; k < nrows; k++) {
        int t;
        control_hits += match_row(&rows[k], &t) >= 0 || t >= 0;
    }
    control_heading = 0;
    printf("states: negative control: with every heading of the model one 2048th off, %d of the rows are found (%s)\n", control_hits, control_hits ? "the comparison cannot tell a heading: the control fails" : "none: it tells every heading");
    /* the length of a take in the original's own clock: the hundredths (2 a frame) between the rows where the cars jump back to the start of a path */
    int ticks_seen[8], nseen = 0, last_take = -1;
    for (int k = 0; k < nrows && nseen < 8; k++) {
        const int f = frame_of[k];
        if (f < 0 || states[f].take == last_take) continue;
        if (last_take >= 0) ticks_seen[nseen++] = rows[k].hund;
        last_take = states[f].take;
    }
    printf("takes: the original's clock (hundredths) at the first row of %d takes after the first:", nseen);
    for (int k = 0; k < nseen; k++) printf(" %d", ticks_seen[k]);
    printf("; the firmware's take has %d frames, the original's clock goes 2 hundredths a frame\n", TAKE_FRAMES);
    int bad_len = 0;
    for (int k = 1; k < nseen; k++) bad_len += ticks_seen[k] - ticks_seen[k - 1] != 2 * TAKE_FRAMES;
    printf("takes: %d of %d lengths are %d frames\n", nseen - 1 - bad_len, nseen - 1, TAKE_FRAMES);

    /* 2. the pictures */
    long sum_cars = 0, sum_eq = 0, sum_near = 0, sum_diff = 0, sum_miss = 0;
    int compared = 0, perfect = 0, bd = 0;
    unsigned sum_pieces = 0, sum_faces = 0;
    for (int s = 0; s < nshots; s++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.idx", argv[3], shots[s].name);
        FILE *h = fopen(path, "rb");
        if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%s.pal", argv[3], shots[s].name);
        h = fopen(path, "rb");
        if (!h || fread(shot_pal, 4, 256, h) != 256) memset(shot_pal, 0, sizeof shot_pal);
        if (h) fclose(h);
        if (!same_palette(shot_pal, arcade_pal)) { printf("%s: palette is not the title's\n", shots[s].name); continue; }
        /* the frames round the request: the original's hundredths at the last row before it say which frame it was (2 a frame, the clock goes round every 4 takes); the poll and
           the screen are not in step (the poll reads the memory a frame ahead or behind, Ctrl+F5 is taken when DOSBox gets to it): 16 frames before, 4 after */
        const int hund = rows[shots[s].at].hund, f0 = (hund / 2 + 1) % (4 * TAKE_FRAMES), brow = shots[s].at;
        int best = 1 << 30, bf = -1;
        for (int dd = -16; dd <= 4; dd++) {
            const int g = (f0 + dd + 4 * TAKE_FRAMES) % (4 * TAKE_FRAMES);
            render(g, hund / 2 + 1 + dd);
            const int d = silhouette_miss();
            if (d < best) { best = d; bf = g; bd = dd; }
        }
        made = -1;                                              /* (again, from the first frame: the best candidate's own picture) */
        render(bf, hund / 2 + 1 + bd);
        const volatile uint8_t *fb = FB(0);
        const int d = diff(), miss = silhouette_miss();
        int cars = 0, eq = 0, near = 0, other = 0;
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                const int i = y * 320 + x;
                if (fb[i] == plain[i] && frame_idx[i] == plain[i]) continue;        /* the picture on both sides */
                cars++;
                if (fb[i] == frame_idx[i]) { eq++; continue; }
                int found = 0;
                for (int dy = -1; dy <= 1 && !found; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int j = i + dy * 320 + dx;
                        if (x + dx >= 0 && x + dx < 320 && y + dy >= 0 && y + dy < 200 && fb[j] == frame_idx[i]) { found = 1; break; }
                    }
                near += found;
                other++;
            }
        printf("%s (%s): frame %d of the clock (take %d, row %d, hundredths %d): %d of 64000 pixels differ, %d of them in one silhouette only; where either picture is not the plain one: %d pixels, "
               "%d equal (%.1f %%), %d of the other %d within a pixel; %u pieces, %u faces\n", shots[s].name, shots[s].label, bf, states[bf].take, brow, hund, d, miss, cars, eq,
               cars ? 100.0 * eq / cars : 100.0, near, other, pieces_drawn, faces_drawn);
        compared++;
        perfect += d == 0;
        sum_cars += cars;
        sum_eq += eq;
        sum_near += near;
        sum_diff += d;
        sum_miss += miss;
        sum_pieces += pieces_drawn;
        sum_faces += faces_drawn;
        if (dump_name && argc > 4 && strstr(shots[s].name, dump_name)) {
            dump(argv[4], shots[s].name, "mine", 0);
            dump(argv[4], shots[s].name, "ref", 0);
            dump(argv[4], shots[s].name, "diff", 1);
        }
        fflush(stdout);
    }
    printf("%d screenshots compared, %d identical; %.1f pixels differ on average, %.1f of them in one silhouette only; where either picture shows a car: %ld pixels, %.1f %% equal, %.1f %% of the others within a pixel; "
           "%.1f pieces and %.0f faces handed to the geometry unit a frame\n", compared, perfect, compared ? (double)sum_diff / compared : 0.0, compared ? (double)sum_miss / compared : 0.0, sum_cars, sum_cars ? 100.0 * sum_eq / sum_cars : 0.0,
           sum_cars - sum_eq ? 100.0 * sum_near / (sum_cars - sum_eq) : 0.0, compared ? (double)sum_pieces / compared : 0.0, compared ? (double)sum_faces / compared : 0.0);
    return wrong || control_hits ? 1 : 0;
}
