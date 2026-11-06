/*
 * The options room (firmware/s2room.c) against the running original: tools/room.sh gives this the raw DOSBox screenshots of tools/s2/dosbox_arcade.py --options (320 x 200 palette
 * indices, tools/pages3d_idx.py) and the states of the original's camera record round each one (tools/room_shots.py: the place, the angles a0 and a1, the animation clock). For every
 * state the firmware's page is drawn exactly as the game has it (the team 1's room built from MENU11.DAT, the backdrop, the placements, the darkened band, the title, the choice's
 * name, the arrows) and compared pixel for pixel with the screenshot; the state that matches best is the screenshot's (a screenshot of a settled station has one camera and
 * only the animation clock to choose; one in the middle of a move may be any frame of the window). Of the settled stations the camera is also moved by a few units of the
 * angle, to show that the record's own is the best (the camera of the engine as s2scene_camera_of() converts it).
 * It reports, per screenshot: the pixels equal, the pixels that differ and have the screenshot's colour within a pixel of the page's (the textures are minified: another texel is
 * picked where the original's matrices (16-bit tables) and ours (floats) differ in the last bit), the pieces and faces the page handed to the geometry unit, and in the end
 * the sums. The 2D parts (the band, the title, the name, the arrows) are compared on their own, in the rows of their boxes.
 *
 * Usage: room_match <SCREAMER2.zip> <shots file> <folder of .idx> [<out prefix: pictures of the best match per screenshot (DUMP=<part of a name or label>)>]
 *        ONLY=<part of a name or label>: only those
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

/* the hardware's stand-ins (as tools/host/trackmap_match.c) */
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

#include "../s2room.c"                                          /* (included: the test reaches the page's drawing) */
#include "../s2pages.c"                                         /* (and the pages of the other stations: the MULTIPLAYER MENU and GAME SETTINGS, drawn by the test) */

static const struct s2room_mem mem = { (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000), 0x400000, (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000), 0x700000, 0, 0 };

static uint8_t frame_idx[S2G_PIC_BYTES];
static uint32_t shot_pal[256];

/* the page as the game has it for a state of the camera record: station, place, angles, animation clock; arrows: the blink phase */
static void render(int at, const int32_t pos[3], int a0, int a1, int anim, int arrows, int texts)
{
    R.cam = (struct s2room_cam){ { pos[0], pos[1], pos[2] }, 0.0f, 0.0f, a0, a1, ROW_MOVE };
    R.fresh = 1;
    R.title = s2gfx_line("OPTIONS ROOM", 0, "OPTIONS ROOM");
    static const char *const names[S2ROOM_STATIONS] = { "ARCADE", "CHAMPIONSHIP", "TIME ATTACK", "MULTIPLAYER", "GAME SETTINGS" };
    for (int k = 0; k < S2ROOM_STATIONS; k++) R.name[k] = s2gfx_line("MAIN MENU", 1 + k, names[k]);
    track_clock_set(((float)anim + 0.5f) / 65535.0f);
    const struct track_cam cam = s2scene_camera_of(R.cam.pos, R.cam.a0, R.cam.a1);
    struct blit_list *l = s2gfx_begin_on(0);
    pieces_drawn = faces_drawn = 0;
    const struct s2room_view view = { &cam, 0, 0, 0, 1, s2room_dark(), 0, BAND_Y, FB_W, BAND_H };
    s2room_scene(l, &view);
    if (texts) room_texts(&R, l, at, arrows ? 0 : 0x40);
    s2gfx_flush();
}

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

/* the boundaries between colour ramps (a pixel whose right or lower neighbour has another ramp: the edges of the room's objects, not of the textures' fine detail): how many of
   the screenshot's have one of ours within a pixel (recall) and how many of ours one of the screenshot's (precision) */
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

static void edges(int *ref_n, int *ref_ok, int *my_n, int *my_ok)
{
    static uint8_t mine[S2G_PIC_BYTES];
    memcpy(mine, (const void *)FB(0), S2G_PIC_BYTES);
    *ref_n = *ref_ok = *my_n = *my_ok = 0;
    for (int y = 0; y < 170; y++)                               /* (above the band: the texts and the band are the page's own) */
        for (int x = 0; x < 320; x++) {
            if (is_edge(frame_idx, x, y)) {
                (*ref_n)++;
                *ref_ok += edge_near(mine, x, y);
            }
            if (is_edge(mine, x, y)) {
                (*my_n)++;
                *my_ok += edge_near(frame_idx, x, y);
            }
        }
}

static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

static void dump(const char *prefix, const char *name, const char *what, int diffmap)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s_%s.rgb", prefix, name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    const volatile uint8_t *fb = FB(0);
    const uint32_t *pal = track_palette();
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (diffmap) c = fb[i] != frame_idx[i] ? 0xFFFFFF : 0;
        else c = shot_pal[strcmp(what, "ref") ? fb[i] : frame_idx[i]];
        (void)pal;
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

struct cand { int32_t pos[3]; int a0, a1, anim, station; };

/* The pages the stations lead to that are 2D: the MULTIPLAYER MENU (the cursor at any of the three items, the item out or not) and GAME SETTINGS (the first row lit or not), drawn by
   the firmware's page functions over their pictures, each compared whole with the screenshot at the best of their states. */
static void page_match(const char *name, const char *label)
{
    static uint32_t pal[256];
    const int multi = strstr(label, "p3_") != 0;
    if (!strstr(label, "p3_") && !strstr(label, "p4_")) {
        printf("%s (%s): a page of another station (not compared here)\n", name, label);
        return;
    }
    if (!s2gfx_bkk(multi ? "BKG_6L.BKK" : "BKG_7L.BKK", s2gfx_pic(0), pal) || !same_palette(shot_pal, pal)) {
        printf("%s (%s): palette is not the page's\n", name, label);
        return;
    }
    if (!multi) {                                               /* the private CHOICE.DAT of the run: the game's own settings, ARCADE ENEMIES SKILL ROOKIE */
        s2save_reset();
        s2save_set(S2S_SKILL, 0);
    }
    int best = 1 << 30, bs = 0;
    for (int state = 0; state < (multi ? 6 : 2); state++) {
        struct blit_list *l = s2gfx_begin_on(0);
        s2gfx_picture(l, s2gfx_pic(0));
        if (multi) {
            struct multi m = { state % 3, (uint32_t)(state / 3) * 0x20, 0, 0 };
            multi_draw(&m, l, 0);
        } else {
            struct settings m = { 0, (uint32_t)state * 0x20 };
            set_draw(&m, l, 0);
        }
        s2gfx_flush();
        const int d = diff();
        if (d < best) { best = d; bs = state; }
    }
    printf("%s (%s): %s page, state %d: %d of 64000 pixels differ\n", name, label, multi ? "MULTIPLAYER MENU" : "GAME SETTINGS", bs, best);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <shots file> <folder of .idx> [out prefix]\n", argv[0]); return 2; }
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
    const int team = getenv("TEAM") && atoi(getenv("TEAM")) >= 1 && atoi(getenv("TEAM")) <= 4 ? atoi(getenv("TEAM")) : 1;      /* TEAM=<1 .. 4>: the room of that team */
    if (!s2room_load(&zip, &m, team)) { fprintf(stderr, "the room\n"); return 1; }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);                       /* (the log: the room's size) */
    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;

    static char names[64][64];
    static char labels[64][48];
    static struct cand cands[64][40];
    static int ncand[64];
    int nn = 0;
    FILE *sf = fopen(argv[2], "r");
    char name[64], label[48];
    struct cand c;
    while (sf && fscanf(sf, "%63s %47s %d %d %d %d %d %d %d", name, label, &c.pos[0], &c.pos[1], &c.pos[2], &c.a0, &c.a1, &c.anim, &c.station) == 9) {
        int k;
        for (k = 0; k < nn; k++) if (!strcmp(names[k], name)) break;
        if (k == nn) {
            if (nn == 64) continue;
            snprintf(names[nn], 64, "%s", name);
            snprintf(labels[nn], 48, "%s", label);
            nn++;
        }
        if (ncand[k] < 40) cands[k][ncand[k]++] = c;
    }
    if (sf) fclose(sf);

    int frames = 0, sum_diff = 0, sum_near = 0, sum_text_n = 0, sum_text_eq = 0, sum_rec_n = 0, sum_rec_ok = 0, sum_pre_n = 0, sum_pre_ok = 0;
    unsigned st_n[S2ROOM_STATIONS] = { 0 }, st_pieces[S2ROOM_STATIONS] = { 0 }, st_faces[S2ROOM_STATIONS] = { 0 };
    static uint8_t scene_only[S2G_PIC_BYTES];
    for (int f = 0; f < nn; f++) {
        if (only && !strstr(names[f], only) && !strstr(labels[f], only)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s.idx", argv[3], names[f]);
        FILE *h = fopen(path, "rb");
        if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%s.pal", argv[3], names[f]);
        h = fopen(path, "rb");
        if (!h || fread(shot_pal, 4, 256, h) != 256) memset(shot_pal, 0, sizeof shot_pal);
        if (h) fclose(h);
        if (strstr(labels[f], "_page")) {
            page_match(names[f], labels[f]);
            continue;
        }
        if (!same_palette(shot_pal, track_palette())) { printf("%s (%s): palette is not the room's\n", names[f], labels[f]); continue; }
        /* the state that matches best (the arrows at the better of the two blink phases) */
        int best = 1 << 30, bk = 0, barrows = 1;
        for (int k = 0; k < ncand[f]; k++)
            for (int arrows = 1; arrows >= 0; arrows--) {
                render(cands[f][k].station, cands[f][k].pos, cands[f][k].a0, cands[f][k].a1, cands[f][k].anim, arrows, 1);
                const int d = diff();
                if (d < best) { best = d; bk = k; barrows = arrows; }
            }
        const struct cand *b = &cands[f][bk];
        render(b->station, b->pos, b->a0, b->a1, b->anim, barrows, 0);                  /* the picture without the texts, to tell which pixels are the texts' */
        memcpy(scene_only, (const void *)FB(0), S2G_PIC_BYTES);
        render(b->station, b->pos, b->a0, b->a1, b->anim, barrows, 1);
        const int near = near_count(), faces = (int)faces_drawn;
        const unsigned pieces = pieces_drawn;
        int text_n = 0, text_eq = 0;
        for (int i = 0; i < S2G_PIC_BYTES; i++)
            if (FB(0)[i] != scene_only[i]) {
                text_n++;
                text_eq += FB(0)[i] == frame_idx[i];
            }
        int rec_n, rec_ok, pre_n, pre_ok;
        edges(&rec_n, &rec_ok, &pre_n, &pre_ok);
        printf("%s (%s): station %d, camera (%d, %d, %d) a0 %d a1 %d: %.1f %% of the pixels differ, %.1f %% of those within a pixel's reach; edges: %.1f %% of the screenshot's have one of ours within "
               "a pixel, %.1f %% of ours one of the screenshot's; texts and arrows: %d of %d pixels equal; %u pieces, %d faces\n", names[f], labels[f], b->station, b->pos[0], b->pos[1], b->pos[2], b->a0,
               b->a1, 100.0 * best / S2G_PIC_BYTES, best ? 100.0 * near / best : 0.0, rec_n ? 100.0 * rec_ok / rec_n : 0.0, pre_n ? 100.0 * pre_ok / pre_n : 0.0, text_eq, text_n, pieces, faces);
        /* a settled station (label st<k>a / b): the record's own angles are the best of a small search */
        if (labels[f][0] == 's' && labels[f][1] == 't') {
            int sbest = best, sa0 = 0, sa1 = 0;
            for (int da0 = -3; da0 <= 3; da0++)
                for (int da1 = -3; da1 <= 3; da1++) {
                    render(b->station, b->pos, (b->a0 + da0) & 0x7FF, (b->a1 + da1) & 0x7FF, b->anim, barrows, 1);
                    const int d = diff();
                    if (d < sbest) { sbest = d; sa0 = da0; sa1 = da1; }
                }
            printf("    the angles moved by up to 3: the best is at (%+d, %+d), %d pixels differing instead of %d\n", sa0, sa1, sbest, best);
            render(b->station, b->pos, b->a0, b->a1, b->anim, barrows, 1);
            st_n[b->station]++;
            st_pieces[b->station] += pieces;
            st_faces[b->station] += (unsigned)faces;
        }
        frames++;
        sum_diff += best;
        sum_near += near;
        sum_text_n += text_n;
        sum_text_eq += text_eq;
        sum_rec_n += rec_n;
        sum_rec_ok += rec_ok;
        sum_pre_n += pre_n;
        sum_pre_ok += pre_ok;
        if (dump_name && argc > 4 && (strstr(names[f], dump_name) || strstr(labels[f], dump_name))) {
            dump(argv[4], names[f], "mine", 0);
            dump(argv[4], names[f], "ref", 0);
            dump(argv[4], names[f], "diff", 1);
        }
        fflush(stdout);
    }
    printf("%d screenshots: %.1f %% of the pixels differ, %.1f %% of those within a pixel of the screenshot's colour; edges: %.1f %% of the screenshots' have one of ours within a pixel, %.1f %% of ours "
           "one of theirs; texts and arrows: %d of %d pixels equal\n", frames, frames ? 100.0 * sum_diff / ((double)frames * S2G_PIC_BYTES) : 0.0, sum_diff ? 100.0 * sum_near / sum_diff : 0.0,
           sum_rec_n ? 100.0 * sum_rec_ok / sum_rec_n : 0.0, sum_pre_n ? 100.0 * sum_pre_ok / sum_pre_n : 0.0, sum_text_eq, sum_text_n);
    for (int k = 0; k < S2ROOM_STATIONS; k++)
        if (st_n[k]) printf("the room at station %d: %u pieces, %u faces handed to the geometry unit a frame (settled)\n", k, st_pieces[k] / st_n[k], st_faces[k] / st_n[k]);
    return 0;
}
