/*
 * The CAR page's carousel (s2showroom.h)
 *
 * Read from STARTL.EXE (0x40c31 .. 0x41a60): at the start the cars k = 0 .. n - 1 are put on a ring, car k at (0, 0, z_k) turned about the vertical by
 * -2 pi / n * k, z_0 = 2800 and the others 3000 (whole numbers, cut), and the places are turned round as many times as the chosen car's number, so that the
 * chosen car stands at (0, 0, 2800). The camera is fixed over that place: (0, 600, 4000), pitched 22.5 degrees down (angle 0x780 of 2048).
 * A frame: car k turns by dt * 512 (the chosen car) or dt * 128 of 2048 (cut to whole numbers each frame); its place glides to its slot,
 * (x, y, z) = (slot x, slot y, slot z) in the CAR state and, in the TRANSMISSION state, (0, y + 200, z + 200) for the chosen car and (0, y - 1200, z - 4000)
 * for the others, 15 % of the way a frame (row 0x46 of the easing table; the chosen car in the TRANSMISSION state 5 %: row 0x5a); a key is taken while
 * the last car is 150 or less from its slot.
 */
#include "s2showroom.h"
#include "s2scene.h"
#include "s2env.h"
#include "s2gfx.h"
#include "s2params.h"
#include "car.h"
#include "dat.h"
#include "dlog.h"
#include "hw.h"
#include "frame.h"
#include <math.h>
#include <string.h>

#define DT          0.02f
#define PI          3.1415927f
#define TWO_PI      6.2831853f
#define ROOM        0x38000u                /* a car's unpacked .MLB and .3DX (the largest pair: 211,000 bytes) */
#define ENV_ROOM    S2_ENV_BYTES(1)         /* a car's windows' picture: ENVM's one frame tiled 256 x 256 and its texture record (s2env.h) */
#define ENV_TEMP    0x10000u                /* ENV.MLB unpacked (33,580 bytes) before it is tiled */
_Static_assert(S2SHOW_MAX * (ROOM + ENV_ROOM) + ENV_TEMP <= S3D_MODELS_MAX, "the showroom's cars, their windows' pictures and the temporary ENV.MLB fit the toolkit's models");

static struct car {
    int32_t pos[3];
    int a1;
    const uint8_t *mlb, *mesh;
    uint32_t mlb_size;
    struct track_refl refl;                 /* the windows' reflection state (the original's object record +0x24 ..) */
} car[S2SHOW_MAX];
static int32_t slot[S2SHOW_MAX][3];
static int n_cars, chosen_car, loaded_team, loaded_n, fresh;
static uint32_t loaded_gen;                 /* s2scene_generation() after the load: another page's models in the region since make the cars gone */

/* ------------------------------------------------------------------------------------------------ the engine's small routines */

static int32_t cut(float f)
{
    if (!(f > -2147483648.0f)) return INT32_MIN;
    if (f >= 2147483648.0f) return INT32_MAX;
    return (int32_t)f;
}

static void ease_int(int32_t *v, float target, int row)
{
    *v = cut(((float)*v - target) * (0.5f + (float)row * 0.005f) + target);
}

/* 0x33703: the n places turned round one: direction 1 (each moves on, the last comes first) or 0 */
static void rotate(int dir)
{
    int32_t tmp[3];
    if (dir) {
        memcpy(tmp, slot[n_cars - 1], sizeof tmp);
        for (int k = n_cars - 1; k > 0; k--) memcpy(slot[k], slot[k - 1], sizeof tmp);
        memcpy(slot[0], tmp, sizeof tmp);
    } else {
        memcpy(tmp, slot[0], sizeof tmp);
        for (int k = 0; k < n_cars - 1; k++) memcpy(slot[k], slot[k + 1], sizeof tmp);
        memcpy(slot[n_cars - 1], tmp, sizeof tmp);
    }
}

/* ------------------------------------------------------------------------------------------------ the cars */

int s2showroom_load(struct zip *z, int team, int n)
{
    if (team < 1 || team > 4 || n < 1 || n > S2SHOW_MAX) return 0;
    if (loaded_team == team && loaded_n >= n && loaded_gen == s2scene_generation()) return 1;
    char path[24] = "GFX/CARS101.DAT";
    path[8] = (char)('0' + team);
    static struct s2_cont c;
    const uint32_t t0 = rdcycle();
    loaded_team = 0;
    uint8_t *base = s2scene_models();
    /* The cars' windows: the game loads the team's GFX/ENVM<team>1.DAT with the team's cars (STARTL.EXE 0x44afa) and routine 160 draws its picture, the
       same for the four teams, through the page's palette. Each car's .MLB gets its own copy (the windows' records are offsets from the car's file),
       after the five cars' rooms: the picture is read first, into the room after them. */
    char env_path[24] = "GFX/ENVM11.DAT";
    env_path[8] = (char)('0' + team);
    uint8_t *envs = base + (uint32_t)S2SHOW_MAX * ROOM, *env_mlb = envs + (uint32_t)S2SHOW_MAX * ENV_ROOM;
    if (!s2scene_open(z, &c, env_path)) return 0;
    const uint32_t env_size = s2scene_entry(&c, "ENV.MLB", env_mlb, ENV_TEMP);
    if (!env_size || s2_env_frames(env_mlb, env_size) != 1) return 0;
    if (!s2scene_open(z, &c, path)) return 0;
    for (int k = 0; k < n; k++) {
        char name[16] = "AUTO1.MLB";
        name[4] = (char)('1' + k);
        uint8_t *to = base + (uint32_t)k * ROOM;
        uint32_t room = ROOM, m = s2scene_entry(&c, name, to, room);
        if (!m) return 0;
        car[k].mlb = to;
        car[k].mlb_size = m;
        m = (m + 255) & ~255u;
        memcpy(name + 5, ".3DX", 5);
        car[k].mesh = to + m;
        if (!s2scene_entry(&c, name, to + m, room - m)) return 0;
        uint8_t *block = envs + (uint32_t)k * ENV_ROOM;
        s2_env_write(block, env_mlb, 1, (uint32_t)(block - to));
        s2_env_windows(to, car[k].mlb_size, (uint32_t)(block - to));
    }
    s2scene_settle();
    loaded_team = team;
    loaded_n = n;
    loaded_gen = s2scene_generation();
    fresh = 1;
    dlog_str("showroom: ");
    dlog_dec(n);
    dlog_str(" cars of team ");
    dlog_dec(team);
    dlog_str(" in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return 1;
}

void s2showroom_init(int n, int chosen)
{
    n_cars = n;
    chosen_car = chosen;
    for (int i = 0; i < n; i++) {
        const float z = i == 0 ? 2800.0f : 3000.0f, a = -(TWO_PI / (float)n) * (float)i;
        slot[i][0] = cut(z * sinf(a));
        slot[i][1] = 0;
        slot[i][2] = cut(z * cosf(a));
    }
    for (int k = 0; k < chosen; k++) rotate(1);
    for (int i = 0; i < n; i++) {
        memcpy(car[i].pos, slot[i], sizeof car[i].pos);
        car[i].a1 = 0;
        car[i].refl = (struct track_refl){ { 0, 0, 0 }, { 0.0f, 0.0f, 0.0f } };
    }
}

int s2showroom_step(int state)
{
    int far = 0;
    for (int i = 0; i < n_cars; i++) {
        struct car *c = &car[i];
        const int me = i == chosen_car;
        c->a1 = cut((float)c->a1 + DT * (me ? 512.0f : 128.0f)) & 0x7FF;
        const int row = me ? state * 0x14 + 0x46 : 0x46;
        ease_int(&c->pos[0], state ? 0.0f : (float)slot[i][0], row);
        ease_int(&c->pos[1], (float)(slot[i][1] + (me ? state * 200 : state ? -0x4B0 : 0)), row);
        ease_int(&c->pos[2], (float)(slot[i][2] + (me ? state * 200 : state ? -0xFA0 : 0)), row);
        far = c->pos[0] - slot[i][0] > 150 || slot[i][0] - c->pos[0] > 150;
    }
    return far;
}

void s2showroom_move(int dir)
{
    if (!n_cars) return;
    chosen_car = (chosen_car + (dir > 0 ? 1 : n_cars - 1)) % n_cars;
    rotate(dir > 0);
}

int s2showroom_chosen(void)
{
    return chosen_car;
}

void s2showroom_place(int i, const int32_t pos[3], int a1)
{
    if (i < 0 || i >= n_cars) return;
    memcpy(car[i].pos, pos, sizeof car[i].pos);
    car[i].a1 = a1;
}

static struct track_cam page_cam(void)                      /* the camera of the CAR pages */
{
    return s2scene_camera(0.0f, 600.0f, 4000.0f, -0.39269908f);
}

/* the cars (all of them on the ring, or car `one` alone at `at` turned a1 if one >= 0) seen from `camera` into the back buffer; a box of the picture and the cars darkened through the table
   before the page's sprites and text are drawn over it (the CPU does it: the units are done first) */
static void showroom_draw(struct blit_list *l, const struct track_cam *camera, int one, const int32_t at[3], int a1, const uint8_t *table, int bx, int by, int bw, int bh)
{
    const int buffer = l->buffer;
    if (!n_cars) {                                          /* (no cars: the picture alone) */
        s2gfx_flush();
        if (table) s2gfx_dark_fb(FB(buffer), bx, by, bw, bh, table);
        blit_start(l, buffer);
        return;
    }
    const struct track_cam cam = *camera;
    int order[S2SHOW_MAX];
    float key[S2SHOW_MAX];
    int m = 0;
    if (one >= 0) {
        order[m++] = one;
    } else {
        for (int i = 0; i < n_cars; i++) {
            const float d = s2scene_dist2(&cam, (float)car[i].pos[0], (float)car[i].pos[1], (float)car[i].pos[2]);
            int j = m++;
            for (; j > 0 && key[j - 1] < d; j--) {                  /* far first */
                order[j] = order[j - 1];
                key[j] = key[j - 1];
            }
            order[j] = i;
            key[j] = d;
        }
    }
    s2scene_begin(car[one >= 0 ? one : 0].mlb, buffer, fresh);
    fresh = 0;
    track_reflection_rate(0.002f);                          /* how fast the windows' picture slides: the page's value (measured: the offsets of the original's cars at their places) */
    for (int j = 0; j < m; j++) {
        const struct car *c = &car[order[j]];
        struct track_block b[CAR_BLOCKS];
        const int32_t *pos = one >= 0 ? at : c->pos;
        const struct car_look lk = { (float)pos[0], (float)pos[2], (float)pos[1], s2scene_rad(one >= 0 ? a1 : c->a1), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 };
        const int nb = car_mesh_draw(c->mesh, &lk, &cam, b);
        s2scene_mlb(c->mlb);
        s2scene_blocks_refl(&cam, b, nb, track_object_reflection(&cam, lk.x, lk.y, lk.z, &car[order[j]].refl));
    }
    s2scene_end();
    if (table) {
        while (RS_DRAW & RS_BUSY)
            ;
        s2gfx_dark_fb(FB(buffer), bx, by, bw, bh, table);
    }
    blit_start(l, buffer);
}

void s2showroom_draw(struct blit_list *l)
{
    const struct track_cam cam = page_cam();
    showroom_draw(l, &cam, -1, 0, 0, 0, 0, 0, 0, 0);
}

void s2showroom_draw_cam(struct blit_list *l, const struct track_cam *camera)
{
    showroom_draw(l, camera, -1, 0, 0, 0, 0, 0, 0, 0);
}

void s2showroom_draw_dark(struct blit_list *l, const uint8_t table[256], int x, int y, int w, int h)
{
    const struct track_cam cam = page_cam();
    showroom_draw(l, &cam, -1, 0, 0, table, x, y, w, h);
}

void s2showroom_draw_one(struct blit_list *l, int k, int32_t x, int32_t y, int32_t z, int a1)
{
    const struct track_cam cam = page_cam();
    const int32_t at[3] = { x, y, z };
    showroom_draw(l, &cam, k >= 0 && k < n_cars ? k : 0, at, a1, 0, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------------------------------------ the cars as objects of a room */

static struct track_block obj_blocks[S2SHOW_MAX][CAR_BLOCKS];
static const uint8_t *unit_mlb;                             /* the material file the unit has now (s2showroom_piece) */

/* the car's blocks, made when it comes up in the drawing (the engine's own order: far first, among the room's pieces) */
static int make_car(int id, const struct track_cam *cam, struct track_object *o)
{
    const struct car *c = &car[id];
    const struct car_look lk = { (float)c->pos[0], (float)c->pos[2], (float)c->pos[1], s2scene_rad(c->a1), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 };
    o->n = car_mesh_draw(c->mesh, &lk, cam, obj_blocks[id]);
    o->b = obj_blocks[id];
    return o->n;
}

int s2showroom_objects(int first, int count, struct track_object *objs, int max)
{
    int n = 0;
    for (int i = first; i < first + count && i < n_cars && n < max; i++)
        objs[n++] = (struct track_object){ (float)car[i].pos[0], (float)car[i].pos[2], (float)car[i].pos[1], 1200.0f, 0, 0, make_car, i, 0, &car[i].refl, 0 };
    return n;
}

void s2showroom_scene_begin(void)
{
    unit_mlb = track_mlb();                                 /* (s2scene_begin sets the room's) */
}

void s2showroom_piece(const struct gu_piece *g)
{
    const uint8_t *want = track_mlb();
    for (int k = 0; k < n_cars; k++)
        if (g->obj >= car[k].mlb && g->obj < car[k].mlb + ROOM) {
            want = car[k].mlb;
            break;
        }
    if (want != unit_mlb) {
        s2scene_mlb(want);
        unit_mlb = want;
    }
    hw_piece(g);
}

int s2showroom_fresh(void)
{
    const int f = fresh;
    fresh = 0;
    return f;
}

/* ------------------------------------------------------------------------------------------------ the plate */

int s2showroom_plate(struct zip *z, int team, int car_no, struct s2show_plate *out)
{
    static struct s2_car_params params[S2_CARS_TOTAL];
    static uint32_t game[0x2000 / 4 + 4];
    static uint8_t text[0x4000];
    static int ok;
    if (!ok) {
        const struct zip_entry *e = zip_find(z, "DATA/GAME.DAT");
        if (!e || e->size > 0x2000 || zip_read(z, e, (uint8_t *)game, sizeof game) || !s2_params_cars_dat((const uint8_t *)game, e->size, text, sizeof text, params)) {
            dlog_str("carousel: DATA/GAME.DAT could not be read\n");
            return 0;
        }
        ok = 1;
    }
    if (team < 1 || team > 4 || car_no < 1 || car_no > 5) return 0;
    const struct s2_car_params *p = &params[(team - 1) * S2_TEAM_CARS + car_no - 1];
    out->max_speed = p->max_speed;
    out->drive = p->traction[3] > 0.8f ? 2 : p->traction[2] > 0.8f ? 1 : 0;
    return 1;
}
