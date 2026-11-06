/*
 * The CAR page's and the CAR CUSTOMIZATION page's room scenes (firmware/s2garage.c) against the running original: tools/garage.sh gives this the recording of
 * tools/s2/dosbox_garage.py (tools/garage_rows.py: the original's camera record, the pages' words and the shadows' records every time one changed, the keys and the screenshots) and the
 * raw DOSBox screenshots (320 x 200 palette indices, tools/pages3d_idx.py).
 *
 * The camera: for every recorded state of the camera record the firmware's camera is set to it and stepped (s2garage_car_step / s2garage_cust_step, with the page's state or the item
 * under the cursor the recording says); the state is one of the next recorded states' of the page if the firmware has the original's camera. The recording is the poll of a running
 * program: a frame's place and angles are written one after the other, so a state read in between is half of one frame and half of the next (told: its place or its angles are those of
 * a frame, not both). Counted: the states whose successor within four recorded states is the firmware's step, the half-written ones, the others; and for the CAR page the shadows
 * (the firmware's, as the camera stood when the last frame ended, against the game's records of them).
 * The picture: for each screenshot the states of the page's camera round the moment it was asked for (0.12 s each way); the firmware draws the scene (the room, the cars and their shadows
 * through the engine's list, the page's 2D parts not) from the camera one step before each, so that the frame it draws is the recorded state (the shadows come from the camera as it stood
 * before the step, as in the game); the pixels of the scene's part of the picture (the rows between the title and the plate, not the list's box or the panel) are compared with the
 * screenshot's, the best state is the screenshot's. Per screenshot: the pixels that differ, how many of those have the screenshot's colour within a pixel of ours, the outlines (the pixels
 * whose right or lower neighbour is of another colour ramp: the edges of the objects) of either within a pixel of the other's; in the end the sums, and the pieces and faces
 * the geometry unit was handed a frame.
 *
 * Usage: garage_match <SCREAMER2.zip> <rows file> <folder of .idx> [<out prefix>]       DUMP=<part of a name or label>: the pictures of those (mine, ref, diff); TEAM=<1 .. 4>; ONLY=<part of a name or label>
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
#include "../s2room.h"
#include "../s2garage.h"
#include "../s2showroom.h"
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

/* the hardware's stand-ins (as tools/host/room_match.c), and the count of the material file's changes the unit would have to wait for */
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
static unsigned pieces_drawn, faces_drawn, mlb_changes;
static uint32_t last_mlb;
void hw_piece(const struct gu_piece *g)
{
    const struct su_view v = { (int16_t)(SU_CENTRE & 0xFFFF), (int16_t)(SU_CENTRE >> 16), SU_SCALE, (int32_t)SU_FAR };
    const struct rs_clip clip = { (int)(RS_CLIP0 & 0xFFFF), (int)(RS_CLIP0 >> 16), (int)(RS_CLIP1 & 0xFFFF), (int)(RS_CLIP1 >> 16) };
    struct gu_stats st = { 0, 0, 0, 0 };
    if (GU_MLB != last_mlb) mlb_changes++;
    last_mlb = GU_MLB;
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

/* ------------------------------------------------------------------------------------------------ the recording */

struct row {
    double t;
    int page, state, item, chosen, sway, a0, a1, x, y, z, shadow[5][3], dt, anim, setup, clock, phase;
    float fade;
};
struct shot { double t; char name[48], png[48]; };
static struct row rows[12000];
static int nrows;
static struct shot shots[200];
static int nshots;

static int read_rows(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *tok[40];
        int n = 0;
        for (char *p = strtok(line, " \r\n"); p && n < 40; p = strtok(0, " \r\n")) tok[n++] = p;
        if (n == 33 && tok[0][0] == 'R' && nrows < 12000) {             /* R t page state item chosen sway a0 a1 x y z, the shadows' 15 numbers, fade dt anim setup clock phase */
            struct row *r = &rows[nrows++];
            r->t = atof(tok[1]);
            r->page = atoi(tok[2]); r->state = atoi(tok[3]); r->item = atoi(tok[4]); r->chosen = atoi(tok[5]); r->sway = atoi(tok[6]);
            r->a0 = atoi(tok[7]); r->a1 = atoi(tok[8]); r->x = atoi(tok[9]); r->y = atoi(tok[10]); r->z = atoi(tok[11]);
            for (int k = 0; k < 15; k++) r->shadow[k / 3][k % 3] = atoi(tok[12 + k]);
            r->fade = (float)atof(tok[27]);
            r->dt = atoi(tok[28]);
            r->anim = atoi(tok[29]);
            r->setup = atoi(tok[30]);
            r->clock = atoi(tok[31]);
            r->phase = atoi(tok[32]);
        } else if (n == 4 && tok[0][0] == 'S' && nshots < 200) {
            shots[nshots].t = atof(tok[1]);
            snprintf(shots[nshots].name, sizeof shots[nshots].name, "%s", tok[2]);
            snprintf(shots[nshots].png, sizeof shots[nshots].png, "%s", tok[3]);
            nshots++;
        }
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the camera */

static struct s2garage_cam cam_of(const struct row *r)
{
    /* the swing's phase is a local of the page (tools/garage_rows.py works it out from the sway's growth) */
    return (struct s2garage_cam){ { r->x, r->y, r->z }, r->a0, r->a1, (float)r->sway * 1e-6f, (float)r->phase * 1e-9f };
}

static void step_page(const struct row *r)
{
    if (r->page) s2garage_cust_step(r->item);
    else s2garage_car_step(r->state != 0);
}

static int same(const struct row *a, const struct s2garage_cam *c)
{
    (void)a;
    return c->pos[0] == a->x && c->pos[1] == a->y && c->pos[2] == a->z && c->a0 == a->a0 && c->a1 == a->a1;
}

/* a state read half way through a frame: the place or the angles are a frame's of the page (the firmware's step from the state before it, or the one before that) */
static int half(const struct row *r, int from)
{
    for (int k = from; k >= 0 && k > from - 6; k--) {
        if (rows[k].page != r->page) break;
        struct s2garage_cam c = cam_of(&rows[k]);
        s2garage_set(rows[k].chosen, &c);
        for (int s = 0; s < 3; s++) {
            step_page(&rows[k]);
            const struct s2garage_cam *m = s2garage_camera();
            if ((m->pos[0] == r->x && m->pos[1] == r->y && m->pos[2] == r->z) || (m->a0 == r->a0 && m->a1 == r->a1)) return 1;
        }
    }
    return 0;
}

static int successor[12000];                                    /* the row whose firmware step is the row i's state, -1 */

static void camera_test(void)
{
    int n[2][3] = { { 0 } }, steps[2][4] = { { 0 } };
    int checked[2] = { 0 }, early[2] = { 0 }, settled[2] = { 0 }, keyed = 0;
    int by_state[3][2] = { { 0 } }, by_item[8][2] = { { 0 } };
    int sh_ok = 0, sh_n = 0;
    for (int i = 0; i < nrows; i++) successor[i] = -1;
    for (int i = 0; i + 1 < nrows; i++) {
        const struct row *r = &rows[i];
        if (r->dt < 19500 || r->dt > 20500) { early[r->page]++; continue; }           /* (the first frames after a page loads take a longer time step in the original: not the firmware's) */
        if (r->page == 0 && r->sway == 0) continue;                                    /* (the page before it) */
        if (r->page == 0 && i > 0 && rows[i - 1].page == 1) continue;
        struct s2garage_cam c = cam_of(r);
        s2garage_set(r->chosen, &c);
        int found = 0;
        step_page(r);
        if (same(r, s2garage_camera()) && r->page == rows[i + 1].page) {            /* (a camera that has settled does not change: the state stands, no later row is its successor) */
            successor[i] = i;
            settled[r->page]++;
            continue;
        }
        s2garage_set(r->chosen, &c);
        for (int s = 1; s <= 4 && !found; s++) {
            step_page(r);
            for (int k = i + 1; k <= i + 3 && k < nrows && !found; k++)
                if (rows[k].page == r->page && same(&rows[k], s2garage_camera()) && rows[k].dt >= 19500 && rows[k].dt <= 20500) {
                    found = s;
                    if (successor[k] < 0) successor[k] = i;
                }
        }
        for (int k = i + 1; k <= i + 3 && k < nrows && !found && r->page == 0; k++) {    /* a key pressed in a frame after this state (the poll may have missed a frame or two before it): the page's state or the car is the next row's when that frame is made */
            if (rows[k].page != 0 || (rows[k].state == r->state && rows[k].chosen == r->chosen)) continue;
            for (int before = 0; before <= 2 && !found; before++) {
                s2garage_set(r->chosen, &c);
                for (int s = 0; s < before; s++) s2garage_car_step(r->state != 0);
                if (rows[k].chosen != r->chosen) s2garage_car_choose(rows[k].chosen);
                s2garage_car_step(rows[k].state != 0);
                if (same(&rows[k], s2garage_camera())) {
                    found = 1;
                    keyed++;
                    successor[k] = i;
                }
            }
        }
        checked[r->page]++;
        if (found) {
            n[r->page][0]++;
            steps[r->page][found - 1]++;
        } else if (half(&rows[i + 1], i)) {
            n[r->page][1]++;
        } else {
            n[r->page][2]++;
            if (getenv("VERBOSE")) printf("  neither: t %.3f page %d state %d item %d chosen %d: a0 %d a1 %d (%d, %d, %d), the next %d %d (%d, %d, %d)\n", r->t, r->page, r->state, r->item, r->chosen, r->a0, r->a1, r->x, r->y, r->z,
                                           rows[i + 1].a0, rows[i + 1].a1, rows[i + 1].x, rows[i + 1].y, rows[i + 1].z);
        }
        if (r->page == 0) by_state[r->state][found ? 0 : 1]++;
        else by_item[r->item][found ? 0 : 1]++;
        if (r->page == 0 && r->fade == 0.0f && found == 1) {                              /* the shadows of the next frame come from the camera of this one */
            for (int k = 0; k < 5; k++) {
                int32_t at[3];
                s2garage_set(r->chosen, &c);
                if (!s2garage_shadow(k, at)) continue;
                sh_n++;
                const struct row *q = &rows[i + 1];
                sh_ok += at[0] == q->shadow[k][0] && at[1] == q->shadow[k][1] && at[2] == q->shadow[k][2];
            }
        }
    }
    for (int p = 0; p < 2; p++)
        printf("%s page: %d recorded states at the firmware's frame time (%d more, the first frames after the page loads, take a longer time step in the original and are not tried), %d of them "
               "settled (the step changes nothing); of the others the firmware's step is a following recorded state of the page in %d (by 1 step %d, 2 steps %d, 3 %d, 4 %d), a half-written state "
               "in %d, neither in %d\n", p ? "CAR CUSTOMIZATION" : "CAR", checked[p] + settled[p], early[p], settled[p], n[p][0], steps[p][0], steps[p][1], steps[p][2], steps[p][3], n[p][1], n[p][2]);
    printf("of the CAR page's, %d are the frame in which a key changed the state or the car (the step of the next one's state and car, the swing's phase restarting at 0.125)\n", keyed);
    printf("CAR page by state (0 cars, 1 set-up, 2 choice): equal / not: %d / %d, %d / %d, %d / %d\n", by_state[0][0], by_state[0][1], by_state[1][0], by_state[1][1], by_state[2][0], by_state[2][1]);
    printf("CAR CUSTOMIZATION by item: ");
    for (int k = 0; k < 8; k++) printf("%d: %d / %d  ", k, by_item[k][0], by_item[k][1]);
    printf("\nshadows (the firmware's, from the camera of the frame before, against the game's records): %d of %d equal\n", sh_ok, sh_n);
}

/* ------------------------------------------------------------------------------------------------ the picture */

static uint8_t shot_idx[S2G_PIC_BYTES], mine[S2G_PIC_BYTES];
static uint32_t shot_pal[256];

static int in_mask(const char *label, int page, int x, int y)
{
    if (page) return x < 246 && y >= 32 && y < 176;                         /* (the panel at x 250, the title above, the name below) */
    if (strstr(label, "list") || strstr(label, "choice")) return y >= 40 && y < 150 && (x < 98 || (x >= 222 && x < 246));
    return y >= 40 && y < 160;
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

static int same_palette(const uint32_t *shot, const uint32_t *pal)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (pal[i] >> (s + 2) & 63)) return 0;
    return 1;
}

/* the scene for row i, from the state before it (so that the frame the firmware draws is the recorded state) */
static void render(int i, int from, int draw_state)
{
    const struct row *r = &rows[i], *b = &rows[from];
    static int at_page = -1;
    if (at_page != r->page) {                                           /* (the page's own start: where its cars stand) */
        if (r->page) s2garage_cust_begin(r->chosen);
        else s2garage_car_begin(r->chosen);
        at_page = r->page;
    }
    struct s2garage_cam c = cam_of(b);
    s2garage_set(r->chosen, &c);
    track_clock_set(((float)r->anim + 0.5f) / 65535.0f);
    struct blit_list *l = s2gfx_begin_on(0);
    pieces_drawn = faces_drawn = mlb_changes = 0;
    last_mlb = GU_MLB;
    if (r->page) s2garage_cust_draw(l, r->item);
    else s2garage_car_draw(l, draw_state, 0, 0, 0, 0, 0);
    s2gfx_flush();
    memcpy(mine, (const void *)FB(0), S2G_PIC_BYTES);
}

/* the pages' 2D parts drawn alone by the firmware's own page code (garage_arcade.c, garage_custom.c), over black: the pixels they drew against the screenshot's, at the best of a few
   clocks of the blinking round the recorded one */
void garage_car_overlay(struct blit_list *l, int team, int car, int state, int setup, int choice, int clock);
void garage_custom_overlay(struct blit_list *l, int item, int clock);

static int overlay_check(const char *label, const struct row *r, int *drawn)
{
    int best = -1;
    static uint8_t ov[S2G_PIC_BYTES];
    for (int dc = -8; dc <= 8; dc += 2) {
        struct blit_list *l = s2gfx_begin_on(0);
        blit_fill(l, 0, 0, FB_W, FB_H, 0);
        if (r->page) garage_custom_overlay(l, r->item, r->clock + dc);
        else garage_car_overlay(l, 1, r->chosen + 1, r->state, r->setup, strstr(label, "choice2") ? 2 : 0, r->clock + dc);
        s2gfx_flush();
        memcpy(ov, (const void *)FB(0), S2G_PIC_BYTES);
        int eq = 0, n = 0;
        for (int i = 0; i < S2G_PIC_BYTES; i++)
            if (ov[i]) {
                n++;
                eq += ov[i] == shot_idx[i];
            }
        if (eq > best) {
            best = eq;
            *drawn = n;
        }
    }
    return best;
}

static void dump(const char *prefix, const char *name, const char *what, const char *label, int page)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s_%s.rgb", prefix, name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (!strcmp(what, "diff")) c = in_mask(label, page, i % 320, i / 320) ? (mine[i] != shot_idx[i] ? 0xFFFFFF : 0) : 0x404040;
        else c = shot_pal[!strcmp(what, "ref") ? shot_idx[i] : mine[i]];
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
    const int team = getenv("TEAM") && atoi(getenv("TEAM")) >= 1 && atoi(getenv("TEAM")) <= 4 ? atoi(getenv("TEAM")) : 1;
    s2save_reset();
    static const int8_t custom1[6] = { 3, 1, 2, 4, 0, 3 }, custom2[6] = { 0, 4, 1, 3, 3, 0 };        /* the run's private CHOICE.DAT (tools/s2/dosbox_garage.py): car 2 of team 1's Custom 1 and 2 */
    s2save_set_setup(1, 2, 0, custom1);
    s2save_set_setup(1, 2, 1, custom2);
    if (!s2garage_open(team, 5)) { fprintf(stderr, "the room or the cars\n"); return 1; }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    s2garage_car_begin(1);
    if (!read_rows(argv[2])) { perror(argv[2]); return 1; }
    printf("%d recorded states, %d screenshots\n", nrows, nshots);
    camera_test();

    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;
    long sum_ov_n = 0, sum_ov_eq = 0, sum_n = 0, sum_diff = 0, sum_near = 0, sum_rec_n = 0, sum_rec_ok = 0, sum_pre_n = 0, sum_pre_ok = 0;
    unsigned sum_pieces = 0, sum_faces = 0, sum_switch = 0, nframes = 0;
    for (int s = 0; s < nshots; s++) {
        if (only && !strstr(shots[s].name, only)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s.idx", argv[3], shots[s].png);
        FILE *h = fopen(path, "rb");
        if (!h || fread(shot_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%s.pal", argv[3], shots[s].png);
        h = fopen(path, "rb");
        if (!h || fread(shot_pal, 4, 256, h) != 256) memset(shot_pal, 0, sizeof shot_pal);
        if (h) fclose(h);
        if (!same_palette(shot_pal, track_palette())) { printf("%s: palette is not the room's (a frame of a fade)\n", shots[s].name); continue; }
        /* the candidate states: the rows within 0.12 s of the request whose predecessor (a row whose step is it) is known */
        int best = 1 << 30, bi = -1, bf = -1, bstate = 0, bn = 0;
        for (int i = 1; i < nrows; i++) {
            if (rows[i].t > shots[s].t + 0.12 || (i + 1 < nrows && rows[i + 1].t < shots[s].t - 0.12) || successor[i] < 0) continue;      /* (a settled camera's state stands until the next row) */
            for (int state = 0; state < 2; state++) {
                if (rows[i].page == 1 && state) continue;
                render(i, successor[i], rows[i].state ? 1 : 0);
                int d = 0, n = 0;
                for (int p = 0; p < S2G_PIC_BYTES; p++)
                    if (in_mask(shots[s].name, rows[i].page, p % 320, p / 320)) {
                        n++;
                        d += mine[p] != shot_idx[p];
                    }
                if (d < best) { best = d; bi = i; bf = successor[i]; bstate = rows[i].state ? 1 : 0; bn = n; }
                break;
            }
        }
        if (bi < 0) { printf("%s: no recorded state with a known predecessor near the request\n", shots[s].name); continue; }
        render(bi, bf, bstate);
        int near = 0, rec_n = 0, rec_ok = 0, pre_n = 0, pre_ok = 0;
        for (int y = 1; y < 199; y++)
            for (int x = 1; x < 319; x++) {
                const int p = y * 320 + x;
                if (!in_mask(shots[s].name, rows[bi].page, x, y)) continue;
                if (mine[p] != shot_idx[p]) {
                    int found = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) found |= mine[p + dy * 320 + dx] == shot_idx[p];
                    near += found;
                }
                if (is_edge(shot_idx, x, y)) { rec_n++; rec_ok += edge_near(mine, x, y); }
                if (is_edge(mine, x, y)) { pre_n++; pre_ok += edge_near(shot_idx, x, y); }
            }
        printf("%s (page %d, state %d, item %d, row t %.3f): %.1f %% of the %d pixels differ, %.1f %% of those within a pixel's reach; edges: %.1f %% of the screenshot's have one of ours within a pixel, "
               "%.1f %% of ours one of the screenshot's; %u pieces, %u faces, %u changes of the material file\n", shots[s].name, rows[bi].page, rows[bi].state, rows[bi].item, rows[bi].t, 100.0 * best / bn, bn,
               best ? 100.0 * near / best : 0.0, rec_n ? 100.0 * rec_ok / rec_n : 0.0, pre_n ? 100.0 * pre_ok / pre_n : 0.0, pieces_drawn, faces_drawn, mlb_changes);
        int ov_n = 0;
        const int ov_eq = overlay_check(shots[s].name, &rows[bi], &ov_n);
        printf("    2D parts of the page: %d of %d pixels equal\n", ov_eq, ov_n);
        sum_ov_n += ov_n; sum_ov_eq += ov_eq;
        sum_n += bn; sum_diff += best; sum_near += near; sum_rec_n += rec_n; sum_rec_ok += rec_ok; sum_pre_n += pre_n; sum_pre_ok += pre_ok;
        sum_pieces += pieces_drawn; sum_faces += faces_drawn; sum_switch += mlb_changes; nframes++;
        if (dump_name && argc > 4 && (strstr(shots[s].name, dump_name))) {
            dump(argv[4], shots[s].name, "mine", shots[s].name, rows[bi].page);
            dump(argv[4], shots[s].name, "ref", shots[s].name, rows[bi].page);
            dump(argv[4], shots[s].name, "diff", shots[s].name, rows[bi].page);
        }
        fflush(stdout);
    }
    if (nframes) printf("2D parts of the pages, all screenshots: %ld of %ld pixels equal to the screenshot's\n", sum_ov_eq, sum_ov_n);
    if (nframes)
        printf("%u screenshots: %.1f %% of the pixels differ, %.1f %% of those within a pixel of the screenshot's colour; edges: %.1f %% of the screenshots' have one of ours within a pixel, %.1f %% of "
               "ours one of theirs; on average %u pieces, %u faces and %u changes of the unit's material file a frame\n", nframes, 100.0 * sum_diff / (double)sum_n, sum_diff ? 100.0 * sum_near / sum_diff : 0.0,
               sum_rec_n ? 100.0 * sum_rec_ok / sum_rec_n : 0.0, sum_pre_n ? 100.0 * sum_pre_ok / sum_pre_n : 0.0, sum_pieces / nframes, sum_faces / nframes, sum_switch / nframes);
    return 0;
}
