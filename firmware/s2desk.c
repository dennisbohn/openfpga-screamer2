/*
 * The championship's desk scene (s2desk.h)
 *
 * Read from STARTL.EXE with tools/dis.sh -f (flat addresses). The page is 0x3bc38 .. 0x3bf79: it picks the scene from the league in hand (0x128a10: 0, 1, 2 -> 1, 2, 3; 3 -> 3;
 * 4 -> 4), loads it (0x11298), plays sound 14 of MENU.DTR unless it is scene 4 (0x3bcfd), sets the frame time to 0.02 s and the page's hundredths to two a frame, and loops
 * (0x3bd56 .. 0x3bebf):
 *     the textures' clock a += dt / SPLINES.TXT's second number (back by 1 at 1; 0x1347a0 = a x 65535)         (0x3bd5b)
 *     the camera, the car (0x11d6f) at the scene's time t, the engine's drawing (0x7ea84, 0x755b8)             (0x3bd96)
 *     t += dt / SPLINES.TXT's first number (6, 8, 8 or 12 seconds), at most 1; from 0.98 on the fade is on      (0x3bdb8)
 *     the picture shown; ENTER (a fresh press, 0x389ae, key 0x10) starts the fade; ESC and the arrows do nothing  (0x3bdf4)
 *     the fade: its level + 2 dt a frame (25 frames), the palette faded to black by it, the music with it; at 1 the page is over (0x3be2a)
 * and at its end the next page is LEAGUE SELECTION (0x4337d) or, after league 4 (0x128a10 = 3) if the team's chosen car (0x128a9c + team x 0x188) is not the fifth, this page again
 * with the league in hand 4 and the chosen car set to the fifth (0x3bf06 .. 0x3bf51). There is no text and no sprite: the page is the engine's picture alone.
 *
 * The scene (0x11298): the file `anims\scene<nn>1.dat` (the 1 is the resolution, 320 x 200), SPLINES.PTH (the paths of the format of MAPn.PTH, s2anim.h) and SPLINES.TXT (two numbers:
 * the scene's seconds and the textures' period, then one row per object: its path "MV_mauto", its mesh "auto0" whose last character is the player's chosen car + '1', class C0), the
 * team's car out of `sccar<team>1.dat` (AUTO<car> .MLB and .3DX), the cars' windows' picture ENV.MLB of the scene file and the team's map `map<team>` (MAP<team>.MMP .UNR .MLB .COL .BCK
 * of the scene file: four maps, one for each team) with the palette of its .COL. The paths are told by the kind at header +0x60 (& 7): 1 a camera ("Camera0n", n - 1 its place in the
 * chain; the paths of the chain take the scene's time in turn, each over the share of the run its header +0x28 .. +0x2c spans: scene 4 cuts between three cameras), 2 the point it
 * looks at ("Camera0n.target"), 4 an object. An object whose kind has bit 3 (8: the scenes 1 to 3's car, which stands) is at the first keys of its three channels, whole numbers,
 * with the heading of header +0x50 (and pitch +0x48 and roll +0x4c, which are 0 in all four files); one without it (scene 4's car, which drives in) is at the path's place at t, the
 * heading the header's all the same, and its wheels turn: the phase of 0x1192e4 goes down by dt x 2 pi x 0.7 a frame (0.7 turns a second), kept in 0 .. 2 pi.
 * The camera is the path's place cut to whole numbers and the look from it at the target's, 0x33905 (s2scene_look_at), the angles x 1024 / pi cut and & 0x7ff, no roll.
 */
#include "s2desk.h"
#include "s2anim.h"
#include "s2scene.h"
#include "s2env.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "car.h"
#include "track.h"
#include "track_build.h"
#include "dat.h"
#include "frame.h"
#include "dlog.h"
#include "hw.h"
#include "files.h"
#include "trackmem.h"
#include <math.h>
#include <string.h>

#define DT              0.02f                   /* the frame time the page sets (0x3bd42) */
#define FADE_FRAMES     25                      /* the fade's level goes up 2 dt a frame (0x1112da) */
#define SKIP_AT         0.98                    /* from this time on the fade is on (0x1112d2) */
#define SOUND_JINGLE    14                      /* MENU.DTR (0x3bd0c): scenes 1 to 3 */
#define BACKDROP_H      154                     /* as the options room (s2room.c): the same engine call draws both */
#define CULL_TOP        (-95)
#define CULL_BOTTOM     205
#define MAP_ENTRIES     5
#define PTH_MAX         4096u                   /* SPLINES.PTH: 2,232 to 4,044 bytes */
#define TXT_MAX         256u                    /* SPLINES.TXT: 44 to 48 bytes */
#define CAR_MLB_MAX     0x24000u                /* a car's .MLB unpacked (the largest of the 32: 138,456) */
#define CAR_ROOM        0x30000u                /* its .MLB and .3DX */
#define ENV_TEMP        0x10000u                /* ENV.MLB unpacked (33,580 bytes) before it is tiled */
#define WHEEL_RATE      0.7                     /* turns a second (0x11014c) */
#define TWO_PI_F        6.2831855f              /* the program's float (0x110148) */
_Static_assert(CAR_ROOM + S2_ENV_BYTES(1) + ENV_TEMP <= S3D_MODELS_MAX, "the car, its windows' picture and the temporary ENV.MLB fit the toolkit's models");

/* ------------------------------------------------------------------------------------------------ the scene's rules */

enum { SLOT_CAM = 0, SLOT_TARGET = 3, SLOT_CAR = 6, SLOTS = 7, CAMS = 3 };

static struct s2anim_paths paths;
static int ncam, car_flag, car_a1, car_ok;      /* cameras in the chain; the car's kind has bit 3; its heading (header +0x50); its path was there */
static float seconds, period;                   /* SPLINES.TXT's two numbers */
static int scene_no;
static float t, t_next, anim, wheels;

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static float rdf(const uint8_t *p)
{
    const int32_t v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* a path's slot by its kind (header +0x60 & 7) and the number in its name ("Camera0n", "Camera0n.target": the two digits at name + 6, the place in the chain is n - 1) */
static int desk_slot(const uint8_t *h)
{
    const int kind = rd32(h + 0x60) & 7;
    if (kind == 4) {
        car_flag = (rd32(h + 0x60) & 8) != 0;
        car_a1 = s2scene_angle(rdf(h + 0x50));
        car_ok = 1;
        return SLOT_CAR;
    }
    if (kind != 1 && kind != 2) return -1;
    const int n = (h[8 + 6] - '0') * 10 + (h[8 + 7] - '0') - 1;
    if (n < 0 || n >= CAMS) return -1;
    if (kind == 1 && n + 1 > ncam) ncam = n + 1;
    return (kind == 1 ? SLOT_CAM : SLOT_TARGET) + n;
}

/* a decimal number at the start of a line: "6", "6.5", "12.2" */
static float number(const uint8_t *p, uint32_t n, uint32_t *at)
{
    double v = 0.0, f = 0.1;
    uint32_t i = *at;
    while (i < n && (p[i] == ' ' || p[i] == '\r' || p[i] == '\n')) i++;
    for (; i < n && p[i] >= '0' && p[i] <= '9'; i++) v = v * 10.0 + (p[i] - '0');
    if (i < n && p[i] == '.')
        for (i++; i < n && p[i] >= '0' && p[i] <= '9'; i++, f *= 0.1) v += f * (p[i] - '0');
    *at = i;
    return (float)v;
}

int s2desk_open(const uint8_t *pth, uint32_t pth_size, const uint8_t *txt, uint32_t txt_size, int scene)
{
    uint32_t at = 0;
    seconds = number(txt, txt_size, &at);
    period = number(txt, txt_size, &at);
    ncam = car_flag = car_a1 = car_ok = 0;
    s2anim_paths_clear(&paths);
    scene_no = scene;
    if (seconds <= 0.0f || period <= 0.0f || !s2anim_paths_load(&paths, pth, pth_size, 0, desk_slot) || !ncam || !car_ok) return 0;
    for (int k = 0; k < ncam; k++) {
        const struct s2anim_path *c = &paths.path[SLOT_CAM + k], *g = &paths.path[SLOT_TARGET + k];
        if (!c->ok || !g->ok || !c->c[0].n || !g->c[0].n || c->end <= c->start || g->end <= g->start) return 0;
    }
    return paths.path[SLOT_CAR].ok && paths.path[SLOT_CAR].c[0].n;
}

float s2desk_seconds(void)
{
    return seconds;
}

/* 0x129d5: the chain's path at t (a path whose run ends by t hands over to the next one) as whole numbers x, y up, z */
static void chain_at(int first, float tt, int32_t out[3])
{
    int k = first;
    while (k + 1 < first + ncam && tt >= paths.path[k].end) k++;
    float p[3];
    s2anim_paths_at(&paths, k, tt, p, 0);
    out[0] = (int32_t)p[0];
    out[1] = (int32_t)p[2];
    out[2] = (int32_t)p[1];
}

void s2desk_step(int frame, struct s2desk_state *st)
{
    if (frame == 0) {
        t = 0.0f;
        anim = 0.0f;
        wheels = 0.0f;
    } else {
        t = t_next;
    }
    anim = (float)((double)DT / (double)period + (double)anim);      /* 0x3bd5b: before the frame is made */
    if (anim >= 1.0f) anim = (float)((double)anim + -1.0);
    chain_at(SLOT_CAM, t, st->cam);
    chain_at(SLOT_TARGET, t, st->aim);
    float pitch, head;
    s2scene_look_at(st->cam, st->aim, &pitch, &head);
    st->a0 = s2scene_angle(pitch);
    st->a1 = s2scene_angle(head);
    float p[3];
    s2anim_paths_at(&paths, SLOT_CAR, car_flag ? 0.0f : t, p, 0);       /* (the first keys: the channels of a car that stands do not move) */
    st->car[0] = (int32_t)p[0];
    st->car[1] = (int32_t)p[2];
    st->car[2] = (int32_t)p[1];
    st->car_a1 = car_a1;
    wheels = (float)((double)wheels - (double)DT * (double)TWO_PI_F * WHEEL_RATE);   /* 0x11ec4: once an object */
    if (wheels < 0.0f) wheels = (float)((double)wheels + (double)TWO_PI_F);
    st->t = t;
    st->anim = anim;
    st->wheels = wheels;
    t_next = (float)((1.0 / (double)seconds) * (double)DT + (double)t);  /* 0x3bdb8: after the drawing */
    if (t_next > 1.0f) t_next = 1.0f;
}

int s2desk_scene(int league)
{
    return league < 0 ? 1 : league >= 4 ? 4 : league == 3 ? 3 : league + 1;        /* 0x3bcb2: 3 -> 2, 4 -> 3, then + 1 */
}

/* ------------------------------------------------------------------------------------------------ loading */

static struct s2_cont scene_c, car_c;           /* the scene's container and the car's: each with its directory (the last 4 KB) */
static uint8_t pth[PTH_MAX] __attribute__((aligned(4)));
static uint8_t txt[TXT_MAX] __attribute__((aligned(4)));
static const uint8_t *car_mlb, *car_mesh;       /* the car in the toolkit's models */
static uint32_t car_mlb_size, car_mesh_size;
static struct track_refl refl;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The team's map out of the scene's container as a container of its own: the five entries MAP<team>.MMP .UNR .COL .MLB .BCK as they are stored (not unpacked), one after the other
   in dst, then the directory the entries' reader (dat.h) wants. The scene's container is 4.2 to 4.8 MB with four maps; the team's is 1.1 to 1.3 MB of it, and the SD card reads
   about 1.2 MB a second, so this is about three seconds less than reading it whole. Returns the container's size, 0 if an entry is missing or does not fit. */
static uint32_t desk_map(struct zip *z, const struct s2_cont *c, int team, uint8_t *dst, uint32_t max)
{
    static const char *const ext[MAP_ENTRIES] = { "MMP", "UNR", "COL", "MLB", "BCK" };
    struct dat_entry d[MAP_ENTRIES];
    uint32_t pos = 0;
    for (int k = 0; k < MAP_ENTRIES; k++) {
        char name[16] = "MAP1.MMP";
        name[3] = (char)('0' + team);
        memcpy(name + 5, ext[k], 3);
        if (!dat_find(c->dir, c->dir_len, name, &d[k])) return 0;
        const uint32_t room = (d[k].stored + 3) & ~3u;
        if (pos + room + ZIP_SLACK > max) return 0;
        if (zip_read_range(z, c->e, d[k].offset, d[k].stored, dst + pos, max - pos)) return 0;
        d[k].offset = pos;
        pos += room;
    }
    if (pos + 32 * MAP_ENTRIES + 4 > max) return 0;
    for (int k = 0; k < MAP_ENTRIES; k++) {
        uint8_t *r = dst + pos + 32 * (uint32_t)k;
        const int len = (int)strlen(d[k].name);
        for (int i = 0; i < 16; i++) r[i] = (uint8_t)((i < len ? d[k].name[i] : 0) ^ 0x3A);
        put32(r + 16, d[k].offset);
        put32(r + 20, d[k].stored);
        put32(r + 24, d[k].size);
        put32(r + 28, d[k].method);
    }
    put32(dst + pos + 32 * MAP_ENTRIES, 32 * MAP_ENTRIES);
    return pos + 32 * MAP_ENTRIES + 4;
}

/* The player's car AUTO<car> out of SCCAR<team>1.DAT (its .MLB and .3DX, 50 to 170 KB stored of the 0.2 to 0.35 MB), and the scene's ENV.MLB tiled for its windows (s2env.h), in the
   toolkit's models */
static int desk_car(struct zip *z, int team, int car)
{
    char path[24] = "ANIMS/SCCAR11.DAT";
    path[11] = (char)('0' + team);
    uint8_t *base = s2scene_models();
    uint8_t *env_mlb = base + CAR_ROOM + S2_ENV_BYTES(1);
    const uint32_t env_size = s2scene_entry(&scene_c, "ENV.MLB", env_mlb, ENV_TEMP);
    if (!env_size || s2_env_frames(env_mlb, env_size) != 1) return 0;
    if (!s2scene_open(z, &car_c, path)) return 0;
    char name[16] = "AUTO1.MLB";
    name[4] = (char)('0' + car);
    uint32_t m = s2scene_entry(&car_c, name, base, CAR_MLB_MAX);
    if (!m) return 0;
    car_mlb = base;
    car_mlb_size = m;
    m = (m + 255) & ~255u;
    memcpy(name + 5, ".3DX", 5);
    car_mesh = base + m;
    car_mesh_size = s2scene_entry(&car_c, name, base + m, CAR_ROOM - m);
    if (!car_mesh_size) return 0;
    uint8_t *block = base + CAR_ROOM;
    s2_env_write(block, env_mlb, 1, (uint32_t)(block - base));
    s2_env_windows(base, car_mlb_size, (uint32_t)(block - base));
    s2scene_settle();
    return 1;
}

static void log_ms(const char *what, uint32_t cycles)
{
    dlog_str(what);
    dlog_dec((int32_t)(cycles / (CPU_HZ / 1000)));
    dlog_str(" ms");
}

/* The scene in memory: the team's map built as a room (track_build_s2 with no cars), the paths and the car. 1, or 0 and the log says which file. */
static int desk_load(struct zip *z, const struct s2room_mem *m, int scene, int team, int car)
{
    if (scene < 1 || scene > S2DESK_SCENES || team < 1 || team > 4 || car < 1 || car > 5) return 0;
    const uint32_t t0 = rdcycle();
    char path[24] = "ANIMS/SCENE011.DAT";                  /* (scene<nn><resolution>: nn the two digits at 11 and 12) */
    path[12] = (char)('0' + scene);
    if (!s2scene_open(z, &scene_c, path)) {
        dlog_str("desk: ");
        dlog_str(path);
        dlog_str(" is not in the ZIP\n");
        return 0;
    }
    struct dat_entry dp, dt;
    if (!dat_find(scene_c.dir, scene_c.dir_len, "SPLINES.PTH", &dp) || dp.size > PTH_MAX || s2scene_entry(&scene_c, "SPLINES.PTH", pth, PTH_MAX) != dp.size ||
        !dat_find(scene_c.dir, scene_c.dir_len, "SPLINES.TXT", &dt) || dt.size > TXT_MAX || s2scene_entry(&scene_c, "SPLINES.TXT", txt, TXT_MAX) != dt.size ||
        !s2desk_open(pth, dp.size, txt, dt.size, scene)) {
        dlog_str("desk: the paths of ");
        dlog_str(path);
        dlog_str(" are not usable\n");
        return 0;
    }
    const uint32_t size = desk_map(z, &scene_c, team, m->container, m->container_max);
    if (!size) {
        dlog_str("desk: the map of ");
        dlog_str(path);
        dlog_str(" could not be read\n");
        return 0;
    }
    const uint32_t t1 = rdcycle();
    const struct s2_files f = { .gfx = m->container, .gfx_size = size };                   /* no cars, no track data: the scenery (track_build.h) */
    struct track_build_info info;
    const uint32_t image = track_build_s2(&f, team, m->image, m->image_max, m->scratch, m->scratch_max, &info);
    while (!(MEM_STATUS & MEM_WRITES_IDLE))
        ;
    dcache_invalidate();
    if (!image || !track_open(m->image) || !track_init()) {
        dlog_str("desk: image not built, stopped at ");
        dlog_str(image ? "its header" : info.fail);
        dlog_char('\n');
        return 0;
    }
    const uint32_t t2 = rdcycle();
    if (!desk_car(z, team, car)) {
        dlog_str("desk: the car could not be read\n");
        return 0;
    }
    dlog_str("desk: scene ");
    dlog_dec(scene);
    dlog_str(" T");
    dlog_dec(team);
    dlog_str(" car ");
    dlog_dec(car);
    dlog_char(' ');
    dlog_dec((int32_t)size);
    dlog_str(" bytes read, image ");
    dlog_dec((int32_t)image);
    log_ms(": read ", t1 - t0);
    log_ms(", built ", t2 - t1);
    log_ms(", car ", rdcycle() - t2);
    dlog_char('\n');
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the page */

static struct desk {
    struct s2desk_state st;
    int fresh, in_car, leaving;
    struct track_object obj;
    struct track_block block[CAR_BLOCKS];
} D;

static void desk_frame(void *self, uint32_t tick)
{
    struct desk *p = self;
    s2desk_step((int)tick, &p->st);
    track_clock_set(p->st.anim);
}

static int desk_key(void *self, uint32_t press)
{
    struct desk *p = self;
    if ((press & S2PAGE_ENTER) || (double)t_next >= SKIP_AT) return 1;       /* the fade begins: ENTER, or the scene is nearly over */
    (void)p;
    return 0;
}

/* a piece of the car is drawn with the car's own material file, the rest with the room's */
static void desk_piece(const struct gu_piece *g)
{
    const int car = g->obj >= car_mesh && g->obj < car_mesh + car_mesh_size;
    if (car != D.in_car) {
        s2scene_mlb(car ? car_mlb : track_mlb());
        D.in_car = car;
    }
    hw_piece(g);
}

static void desk_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct desk *p = self;
    (void)tick;
    const int buffer = l->buffer;
    const struct track_cam cam = s2scene_camera_of(p->st.cam, p->st.a0, p->st.a1);
    const struct rs_clip backdrop = { 0, 0, FB_W, BACKDROP_H }, cull = { 0, CULL_TOP, FB_W, CULL_BOTTOM };
    struct track_cam sky = cam;
    sky.yaw += 1.5707964f;                                      /* (the same engine call as the options room's: its backdrop turns a quarter of a turn further than the race's) */
    track_backdrop_blit(l, &backdrop, &sky);
    blit_fill(l, 0, BACKDROP_H, FB_W, FB_H - BACKDROP_H, track_ground_colour());
    struct track_fx fx;
    if (p->fresh && track_fx(&fx)) smg_load(fx.table);
    /* the car: a track object, so that it is sorted in among the room's pieces as the engine sorts one */
    const struct car_look lk = { (float)p->st.car[0], (float)p->st.car[2], (float)p->st.car[1], s2scene_rad(p->st.car_a1), 0.0f, 0.0f, 0.0f, 0.0f,
                                 scene_no == 4 ? p->st.wheels : 0.0f, 0.0f, 0 };
    const int nb = car_mesh_draw(car_mesh, &lk, &cam, p->block);
    p->obj = (struct track_object){ lk.x, lk.y, lk.z, 1200.0f, nb, p->block, 0, 0, 0, &refl, 0 };
    s2scene_begin(track_mlb(), buffer, p->fresh);
    p->fresh = 0;
    p->in_car = 0;
    track_mode = TRACK_HW;
    track_hw_piece = desk_piece;
    track_reflection_rate(0.002f);
    struct track_stats ts;
    track_draw_cam(0, &cull, &cam, &p->obj, nb ? 1 : 0, &ts);
    s2scene_end();
    while (RS_DRAW & RS_BUSY)
        ;
    blit_start(l, buffer);
}

/* a black screen while the scene is read and built */
static void blank(void)
{
    static const uint32_t black[S2G_PALETTE];
    struct blit_list *l = s2gfx_begin();
    blit_fill(l, 0, 0, FB_W, FB_H, 0);
    s2gfx_end(black, 0x8000);
}

int s2desk_run(struct zip *z, const struct s2room_mem *m, int scene, int team, int car)
{
    char name[24] = "SCENE011.DAT";
    name[6] = (char)('0' + scene);
    if (!s2gfx_ready() || scene < 1 || scene > S2DESK_SCENES || !zip_find(z, name)) return 0;      /* (ANIMS is optional in the ZIP: no scene, no black screen) */
    blank();
    loaded.valid = 0;                                           /* (the window's image is the scene's from now on) */
    s2room_forget();
    if (!desk_load(z, m, scene, team, car)) return 0;
    struct desk *p = &D;
    memset(p, 0, sizeof *p);
    p->fresh = 1;
    memset(&refl, 0, sizeof refl);
    if (scene != 4) s2sound_menu(SOUND_JINGLE);
    const struct s2_page page = { 0, track_palette(), 0, FADE_FRAMES, desk_frame, desk_key, desk_draw };
    s2page_run(&page, p);
    return 1;
}

int s2desk_won(int league, int team, int car)
{
    static const struct s2room_mem mem = { GFX_MEM, GFX_MAX, (uint8_t *)(uintptr_t)TEX_BASE, TEX_MAX, SCRATCH_MEM, SCRATCH_MAX };
    struct zip *z = files_zip();
    s2desk_run(z, &mem, s2desk_scene(league), team, car);
    if (league != 3 || car == 5) return car;                    /* (0x3bf06: after league 4, the second visit unless the chosen car is the fifth already) */
    s2desk_run(z, &mem, s2desk_scene(4), team, 5);
    return 5;
}
