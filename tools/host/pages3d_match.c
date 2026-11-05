/*
 * The 3D pages of the firmware against the running original: tools/pages3d.sh gives this the raw DOSBox screenshots of tools/s2/dosbox_arcade.py --pages3d
 * (320 x 200 palette indices, tools/pages3d_idx.py) and it looks, for each one, for the state of the page that draws it: the objects stand where the page
 * puts them once they have settled (the badges on their slots, the letter ring, the cars on their places), their angles are what a screenshot of a
 * turning object cannot tell, so they are searched (every one of the 2048 angles of the engine; coordinate by coordinate, each object's angle alone
 * with the others where they are, twice round). A state is matched when the page's own drawing (s2scene.c, track.c's pieces, the geometry unit's
 * C model, the picture of the page) at its angles gives the same pixels as the screenshot in the part of the screen the page's 3D can reach.
 * The text, the sprites and the frames that were caught in a move are left out: a frame is reported as not settled.
 *
 * Usage: pages3d_match <SCREAMER2.zip> <folder of .idx / .pal> [<out prefix: picture of the best match and of its difference per frame>]
 */
#define _GNU_SOURCE
#include "../hw.h"
#include "../s2scene.h"
#include "../s2gfx.h"
#include "../s2showroom.h"
#include "../s2env.h"
#include "../s2menu.h"
#include "../slot.h"
#include "../dlog.h"
#include "../dat.h"
#include "../car.h"
#include "../geom.h"
#include "../raster.h"
#include "../frame.h"
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

/* the hardware's stand-ins (as tools/host/pages3d_screen.c) */
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
static int refl_force = -1;                     /* REFL_FIT: the windows' offsets tried (u << 16 | v << 24), -1: the page's own (none) */
void hw_piece(const struct gu_piece *gp)
{
    struct gu_piece pc = *gp;
    const struct gu_piece *g = &pc;
    if (refl_force >= 0) pc.refl = (pc.refl & 0xFFFF) | (uint32_t)refl_force;
    const struct su_view v = { (int16_t)(SU_CENTRE & 0xFFFF), (int16_t)(SU_CENTRE >> 16), SU_SCALE, (int32_t)SU_FAR };
    const struct rs_clip clip = { (int)(RS_CLIP0 & 0xFFFF), (int)(RS_CLIP0 >> 16), (int)(RS_CLIP1 & 0xFFFF), (int)(RS_CLIP1 >> 16) };
    struct gu_stats st = { 0, 0, 0, 0 };
    gu_draw(FB((GU_MODE >> 8) & 1), &clip, &v, (const uint8_t *)(uintptr_t)(SDRAM_BASE + GU_MLB), g, &st);
}
void tl_mark(uint32_t t) { (void)t; }
void tl_begin(uint32_t t) { (void)t; }
void s2sound_menu(int id) { (void)id; }
void s2sound_effects(int32_t f) { (void)f; }
void music_frame(void) {}

/* ------------------------------------------------------------------------------------------------ the scenes */

#define MAXOBJ 40
struct item {
    const uint8_t *mesh, *mlb;
    int32_t pos[3];
    int a0, a1;
    uint32_t phase;
    int group;                          /* items of a group share the angle that is searched; -1: the angle is fixed */
    int car;                            /* drawn as a car (car_mesh_draw) */
};
struct scene {
    const char *name;
    struct item it[MAXOBJ];
    int n, groups;
    struct track_cam cam;
    int x0, x1, y0, y1;                 /* the part of the screen compared */
    const uint8_t *pic;
};

static uint8_t picture[2][S2G_PIC_BYTES];
static uint32_t pal[2][256];
static uint8_t frame_idx[S2G_PIC_BYTES];

static void draw_scene(const struct scene *s, const int *angle)
{
    struct blit_list *l = s2gfx_begin_on(0);
    s2gfx_picture(l, s->pic);
    int order[MAXOBJ], m = 0;
    float key[MAXOBJ];
    for (int i = 0; i < s->n; i++) {
        const float d = s2scene_dist2(&s->cam, (float)s->it[i].pos[0], (float)s->it[i].pos[1], (float)s->it[i].pos[2]);
        int j = m++;
        for (; j > 0 && key[j - 1] < d; j--) { order[j] = order[j - 1]; key[j] = key[j - 1]; }
        order[j] = i;
        key[j] = d;
    }
    s2scene_begin(s->it[0].mlb, 0, 1);
    track_reflection_rate(0.002f);                  /* the CAR page's cars' windows: the offsets of a car that stands at its place (the page's own rate, firmware/s2showroom.c) */
    for (int j = 0; j < m; j++) {
        const struct item *it = &s->it[order[j]];
        const int a1 = it->group >= 0 ? angle[it->group] : it->a1;
        struct track_block b[CAR_BLOCKS];
        int nb = 1;
        if (it->car) {
            const struct car_look lk = { (float)it->pos[0], (float)it->pos[2], (float)it->pos[1], s2scene_rad(a1), 0, 0, 0, 0, 0, 0, 0 };
            nb = car_mesh_draw(it->mesh, &lk, &s->cam, b);
        } else {
            s2scene_object(it->mesh, (float)it->pos[0], (float)it->pos[1], (float)it->pos[2], it->a0, a1, it->phase, &b[0]);
        }
        s2scene_mlb(it->mlb);
        struct track_refl st = { { 0, 0, 0 }, { 0.0f, 0.0f, 0.0f } };      /* (a car that has stood since the page began: the first drawing's whole place) */
        s2scene_blocks_refl(&s->cam, b, nb, it->car ? track_object_reflection(&s->cam, (float)it->pos[0], (float)it->pos[2], (float)it->pos[1], &st) : 0);
    }
}

static int diff(const struct scene *s)
{
    const volatile uint8_t *fb = FB(0);
    int d = 0;
    for (int y = s->y0; y < s->y1; y++)
        for (int x = s->x0; x < s->x1; x++) d += fb[y * 320 + x] != frame_idx[y * 320 + x];
    return d;
}

/* of the differing pixels how many are in the objects' outline (one picture shows the background there, the other an object) and how many inside it; the pixels the
   objects cover in the screenshot */
static void outline(const struct scene *s, int *sil, int *inner, int *near, int *covered)
{
    const volatile uint8_t *fb = FB(0);
    *sil = *inner = *near = *covered = 0;
    for (int y = s->y0; y < s->y1; y++)
        for (int x = s->x0; x < s->x1; x++) {
            int i = y * 320 + x, mine = fb[i] != s->pic[i], ref = frame_idx[i] != s->pic[i];
            *covered += ref;
            if (mine != ref) (*sil)++;
            else if (mine && fb[i] != frame_idx[i]) {
                (*inner)++;
                int found = 0;
                for (int dy = -1; dy <= 1; dy++)                /* the screenshot's pixel is one of mine within a pixel's reach: the colour is right, the texel a little off */
                    for (int dx = -1; dx <= 1; dx++)
                        if (fb[i + dy * 320 + dx] == frame_idx[i]) found = 1;
                *near += found;
            }
        }
}

static int cost(const struct scene *s, const int *angle)
{
    draw_scene(s, angle);
    return diff(s);
}

/* the best angles by coordinate descent: a coarse pass over the 2048 angles in steps of 16, a fine pass within 16 of the best, twice round */
static int search(const struct scene *s, int *angle)
{
    int best = 1 << 30;
    for (int g = 0; g < s->groups; g++) angle[g] = 0;
    for (int round = 0; round < 2; round++)
        for (int g = 0; g < s->groups; g++) {
            int keep = angle[g], bc = 1 << 30, ba = keep;
            for (int a = 0; a < 2048; a += 16) {
                angle[g] = a;
                int c = cost(s, angle);
                if (c < bc) { bc = c; ba = a; }
            }
            int centre = ba;
            for (int a = centre - 15; a <= centre + 15; a++) {
                angle[g] = (a + 2048) & 2047;
                int c = cost(s, angle);
                if (c < bc) { bc = c; ba = angle[g]; }
            }
            angle[g] = ba;
            best = bc;
        }
    return best;
}

/* the screen in FB(0) (mine) or the screenshot (ref) with the palette of the page, as a raw RGB file for tools/rgb2png.py; the difference as white on black */
static void dump(const char *prefix, const char *name, const char *what, const uint32_t *p, int diffmap, const struct scene *s)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%.*s_%s.rgb", prefix, (int)(strlen(name) - 4), name, what);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint32_t c;
        if (diffmap) c = fb[i] != frame_idx[i] && i / 320 >= s->y0 && i / 320 < s->y1 ? 0xFFFFFF : 0;
        else c = p[strcmp(what, "ref") ? fb[i] : frame_idx[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

/* ------------------------------------------------------------------------------------------------ the pages' states */

static uint8_t *models;
static const uint8_t *logo_mlb, *badge[4], *letter;
static const uint8_t *car_mlb[5], *car_mesh[5];
static int loaded_team;

static int cut(float f) { return (int)f; }

/* what the original's glide (0x3364d, 0x11212: a whole number eased towards its target, the row's share of the way a frame, cut towards zero each time) comes to:
   it stops a few units short of the target on the side it came from (9 at row 0x50, 7 at row 0x46) */
static int32_t settled(int32_t v, float target, int row)
{
    for (int i = 0; i < 400; i++) v = cut(((float)v - target) * (0.5f + (float)row * 0.005f) + target);
    return v;
}

static int load_logo(void)
{
    static struct s2_cont c;
    uint8_t *to = models;
    uint32_t n;
    if (!s2scene_open(&zip, &c, "GFX/LOGO1.DAT")) return 0;
    logo_mlb = to;
    n = s2scene_entry(&c, "LOGO.MLB", to, S3D_MODELS_MAX);
    to += (n + 255) & ~255u;
    for (int i = 0; i < 4; i++) {
        char nm[16] = "A1LOG-00.3DX";
        nm[1] = (char)('1' + i);
        badge[i] = to;
        n = s2scene_entry(&c, nm, to, 0x10000);
        if (!n) return 0;
        to += (n + 255) & ~255u;
    }
    letter = to;
    return s2scene_entry(&c, "ALETTER.3DX", to, 0x10000) != 0;
}

static int load_cars(int team)
{
    if (loaded_team == team) return 1;
    static struct s2_cont c;
    char path[24] = "GFX/CARS101.DAT";
    path[8] = (char)('0' + team);
    /* (the cars go behind the logos' models, their windows' pictures behind the cars: the team's ENVM, as the page loads it) */
    uint8_t *base = models + 0x100000, *envs = base + 5 * 0x38000, *env_mlb = envs + 5 * S2_ENV_BYTES(1);
    char env_path[24] = "GFX/ENVM11.DAT";
    env_path[8] = (char)('0' + team);
    if (!s2scene_open(&zip, &c, env_path)) return 0;
    const uint32_t env_size = s2scene_entry(&c, "ENV.MLB", env_mlb, 0x10000);
    if (!env_size || s2_env_frames(env_mlb, env_size) != 1) return 0;
    if (!s2scene_open(&zip, &c, path)) return 0;
    for (int k = 0; k < 5; k++) {
        char nm[16] = "AUTO1.MLB";
        nm[4] = (char)('1' + k);
        uint8_t *to = base + (uint32_t)k * 0x38000;
        uint32_t m = s2scene_entry(&c, nm, to, 0x38000);
        car_mlb[k] = to;
        const uint32_t mlb_size = m;
        m = (m + 255) & ~255u;
        memcpy(nm + 5, ".3DX", 5);
        car_mesh[k] = to + m;
        if (!m || !s2scene_entry(&c, nm, to + m, 0x38000 - m)) return 0;
        uint8_t *block = envs + (uint32_t)k * S2_ENV_BYTES(1);
        s2_env_write(block, env_mlb, 1, (uint32_t)(block - to));
        s2_env_windows(to, mlb_size, (uint32_t)(block - to));
    }
    loaded_team = team;
    return 1;
}

/* the team page's slots, turned round rot times (the page's own rule: 0x3ffb9 .. 0x40182) */
static void slots_team(int rot, int32_t slot[4][3])
{
    for (int i = 0; i < 4; i++) {
        slot[i][0] = i == 0 ? 0 : (i - 1) * 3000 - 3000;
        slot[i][1] = i == 0 ? 300 : i == 2 ? 2000 : 0;
        slot[i][2] = i == 0 ? 3500 : 1000;
    }
    for (int r = 0; r < rot; r++) {
        int32_t t[3];
        memcpy(t, slot[3], sizeof t);
        for (int k = 3; k > 0; k--) memcpy(slot[k], slot[k - 1], sizeof t);
        memcpy(slot[0], t, sizeof t);
    }
}

/* TEAM SELECTION with team rot in front: the four badges, each its own angle */
static void scene_team(struct scene *s, int rot)
{
    int32_t slot[4][3];
    slots_team(rot, slot);
    s->n = 4;
    s->groups = 4;
    for (int i = 0; i < 4; i++)
        s->it[i] = (struct item){ badge[i], logo_mlb, { settled(0, (float)slot[i][0], 0x50), settled(0, (float)slot[i][1], 0x50), settled(0, (float)slot[i][2], 0x50) }, 0, 0, 0, i, 0 };
    s->cam = s2scene_camera(0.0f, 500.0f, 4800.0f, 0.0f);
    s->x0 = 0; s->x1 = 320; s->y0 = 32; s->y1 = 168;
    s->pic = picture[0];
    s->name = "team";
}

/* ENTER YOUR NAME with letter cur in front, the ring settled: the chosen team's badge up at its slot + (0, 500, -500) (one angle), the ring's 26 tyres on
   their places (the page's rule, 0x3625d: radius 3500, the angle (k - cur) * -2 pi / 26, the heading the same the other way, the tilts -0.314 and 2.827) */
static void scene_name(struct scene *s, int team, int cur)
{
    int32_t slot[4][3];
    slots_team(team, slot);
    s->n = 0;
    s->groups = 1;
    {
        int32_t at[3];
        for (int k = 0; k < 3; k++) at[k] = settled(0, (float)slot[team][k], 0x50);
        at[1] = settled(at[1], (float)(slot[team][1] + 500), 0x50);
        at[2] = settled(at[2], (float)(slot[team][2] - 500), 0x50);
        at[0] = settled(at[0], (float)slot[team][0], 0x50);
        s->it[s->n++] = (struct item){ badge[team], logo_mlb, { at[0], at[1], at[2] }, 0, 0, 0, 0, 0 };
    }
    for (int k = 0; k < 26; k++) {
        float f1 = (float)(k - cur);
        if (f1 < 0.0f) f1 += 26.0f;
        const float step = 0.2416609823703766f, ang = f1 * -step, tilt = k == cur ? -0.3141592741012573f : 2.8274335861206055f;
        s->it[s->n++] = (struct item){ letter, logo_mlb, { cut(3500.0f * sinf(ang)), 0, cut(3500.0f * cosf(ang)) }, cut(tilt * 1024.0f / 3.1415927f) & 0x7FF,
                                        cut(f1 * step * 1024.0f / 3.1415927f) & 0x7FF, ((uint32_t)k * 0x9D8u + 0x226u) >> 2, -1, 0 };
    }
    s->cam = s2scene_camera(0.0f, 500.0f, 4800.0f, 0.0f);
    s->x0 = 0; s->x1 = 320; s->y0 = 32; s->y1 = 170;
    s->pic = picture[0];
    s->name = "name";
}

/* the CAR page with car ch (0 .. 4) chosen: the five cars on their places, the chosen one's heading searched (the others at 0: they are small and far), compared
   in the part of the screen the chosen car fills */
static void scene_car(struct scene *s, int ch)
{
    int32_t slot[5][3];
    for (int i = 0; i < 5; i++) {
        const float z = i == 0 ? 2800.0f : 3000.0f, a = -(6.2831853f / 5.0f) * (float)i;
        slot[i][0] = cut(z * sinf(a));
        slot[i][1] = 0;
        slot[i][2] = cut(z * cosf(a));
    }
    for (int r = 0; r < ch; r++) {
        int32_t t[3];
        memcpy(t, slot[4], sizeof t);
        for (int k = 4; k > 0; k--) memcpy(slot[k], slot[k - 1], sizeof t);
        memcpy(slot[0], t, sizeof t);
    }
    s->n = 5;
    s->groups = 1;
    for (int i = 0; i < 5; i++)
        s->it[i] = (struct item){ car_mesh[i], car_mlb[i], { slot[i][0], slot[i][1], slot[i][2] }, 0, 0, 0, i == ch ? 0 : -1, 1 };
    s->cam = s2scene_camera(0.0f, 600.0f, 4000.0f, -0.39269908f);
    s->x0 = 60; s->x1 = 260; s->y0 = 60; s->y1 = 150;
    s->pic = picture[1];
    s->name = "car";
}

/* DOSBox's screenshot expands the VGA's 6-bit values to 8 (v << 2 | v >> 4): the same colours when the 6 bits are */
static int same_palette(const uint32_t *shot, const uint32_t *mine)
{
    for (int i = 0; i < 256; i++)
        for (int s = 0; s < 24; s += 8)
            if ((shot[i] >> (s + 2) & 63) != (mine[i] >> (s + 2) & 63)) return 0;
    return 1;
}

static struct scene sc;
static int best_angles[MAXOBJ];

/* the best state of one kind of scene over its hypotheses: how many pixels differ, the hypothesis */
static int try_hyps(int kind, int first, int last, int *which)
{
    int best = 1 << 30, ang[MAXOBJ];
    for (int h = first; h <= last; h++) {
        if (kind == 0) scene_team(&sc, h);
        else if (kind == 1) scene_name(&sc, 0, h);
        else scene_car(&sc, h);
        int c = search(&sc, ang);
        if (c < best) { best = c; *which = h; memcpy(best_angles, ang, sizeof ang); }
    }
    return best;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <folder> [out prefix]\n", argv[0]); return 2; }
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
    models = s2scene_models();
    s2gfx_bkk("LOGOL.BKK", picture[0], pal[0]);
    s2gfx_bkk("BKG_2L.BKK", picture[1], pal[1]);
    const char *only = getenv("ONLY"), *dump_name = getenv("DUMP");
    if (only && !*only) only = 0;
    if (dump_name && !*dump_name) dump_name = 0;
    static char names[256][64];
    int nn = 0;
    DIR *dir = opendir(argv[2]);
    struct dirent *de;
    while (dir && (de = readdir(dir)) && nn < 256)
        if (strstr(de->d_name, ".idx")) snprintf(names[nn++], 64, "%s", de->d_name);
    closedir(dir);
    for (int i = 0; i < nn; i++)
        for (int j = i + 1; j < nn; j++)
            if (strcmp(names[i], names[j]) > 0) { char t[64]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t); }
    const char *team_env = getenv("TEAM");
    int logo_ok = load_logo(), cars_ok = load_cars(team_env && *team_env ? atoi(team_env) : 1);
    if (!logo_ok || !cars_ok) { fprintf(stderr, "LOGO1.DAT %d, CARS101.DAT %d\n", logo_ok, cars_ok); return 1; }
    int frames = 0, exact_outline = 0;
    for (int f = 0; f < nn; f++) {
        if (only && !strstr(names[f], only)) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", argv[2], names[f]);
        FILE *h = fopen(path, "rb");
        if (!h || fread(frame_idx, 1, S2G_PIC_BYTES, h) != S2G_PIC_BYTES) { fprintf(stderr, "%s\n", path); return 1; }
        fclose(h);
        snprintf(path, sizeof path, "%s/%.*s.pal", argv[2], (int)(strlen(names[f]) - 4), names[f]);
        uint32_t fp[256];
        h = fopen(path, "rb");
        if (h) { if (fread(fp, 4, 256, h) != 256) memset(fp, 0, sizeof fp); fclose(h); }
        const int logo = same_palette(fp, pal[0]), carp = same_palette(fp, pal[1]);
        printf("%s: palette %s", names[f], logo ? "LOGOL" : carp ? "BKG_2L" : "other");
        if (!logo && !carp) { printf("\n"); continue; }
        int kind_best = -1, which = 0, bw = 0, bc = 1 << 30, bang[MAXOBJ];
        for (int kind = logo ? 0 : 2; kind <= (logo ? 1 : 2); kind++) {
            int c = kind == 0 ? try_hyps(0, 0, 3, &which) : kind == 1 ? try_hyps(1, 11, 14, &which) : try_hyps(2, 0, 4, &which);
            if (c < bc) { bc = c; kind_best = kind; bw = which; memcpy(bang, best_angles, sizeof bang); }
        }
        if (kind_best == 0) scene_team(&sc, bw);
        else if (kind_best == 1) scene_name(&sc, 0, bw);
        else scene_car(&sc, bw);
        draw_scene(&sc, bang);
        if (getenv("REFL_FIT") && *getenv("REFL_FIT")) {                               /* the offsets of the windows' reflection that make the pixels of a box equal the screenshot's most */
            int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            sscanf(getenv("REFL_FIT"), "%d,%d,%d,%d", &bx0, &by0, &bx1, &by1);
            int best_eq = -1, best_o = 0, none_eq = 0, try_u = -1, try_v = 0, try_eq = 0;
            if (getenv("REFL_TRY")) sscanf(getenv("REFL_TRY"), "%d,%d", &try_u, &try_v);
            for (int o = -1; o < 65536; o++) {
                refl_force = o < 0 ? -1 : (o & 255) << 16 | (o >> 8) << 24;
                draw_scene(&sc, bang);
                const volatile uint8_t *fb = FB(0);
                int eq = 0;
                for (int y = by0; y < by1; y++)
                    for (int x = bx0; x < bx1; x++) eq += fb[y * 320 + x] == frame_idx[y * 320 + x];
                if (o == (try_u & 255) + (try_v & 255) * 256 && try_u >= 0) try_eq = eq;
                if (o < 0) none_eq = eq;
                else if (eq > best_eq) { best_eq = eq; best_o = o; }
            }
            printf("  REFL_FIT box %d,%d,%d,%d (%d pixels): no offsets %d equal, best offsets u %d v %d %d equal\n", bx0, by0, bx1, by1, (bx1 - bx0) * (by1 - by0), none_eq,
                   best_o & 255, best_o >> 8, best_eq);
            if (try_u >= 0) printf("  REFL_TRY u %d v %d: %d equal\n", try_u, try_v, try_eq);
            refl_force = -1;
            draw_scene(&sc, bang);
        }
        int sil, inner, near, covered;
        outline(&sc, &sil, &inner, &near, &covered);
        printf("  %s page (%s %d): %d of %d object pixels differ (%d at the outline, %d inside, of those %d within a pixel); angle %d\n", sc.name,
               kind_best == 0 ? "team" : kind_best == 1 ? "letter" : "car", bw + (kind_best == 0 ? 1 : 0), bc, covered, sil, inner, near, bang[0]);
        frames++;
        if (covered && sil * 20 <= covered) exact_outline++;
        if (dump_name && argc > 3 && strstr(names[f], dump_name)) {
            dump(argv[3], names[f], "mine", logo ? pal[0] : pal[1], 0, &sc);
            dump(argv[3], names[f], "ref", logo ? pal[0] : pal[1], 0, &sc);
            dump(argv[3], names[f], "diff", pal[0], 1, &sc);
        }
        fflush(stdout);
    }
    printf("%d frames matched, %d of them with the outline within 5 %% of the object's pixels\n", frames, exact_outline);
    return 0;
}
