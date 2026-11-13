/*
 * The player's car of Screamer 2 (s2car.h): the game's car model in single precision.
 *
 * Ported routine by routine from OpenScreamer's hand-written text (port/src/s2r_car.h, which is the game's
 * program in the order of its sums; docs/NOTES2.md S2-5f .. S2-8, S2-5zn). The car is the game's 0xCC0-byte
 * record, kept word for word where the game keeps it, so that the routines below read like theirs: RF(o) and
 * RI(o) are the float and the whole number at byte offset o; the comment at each routine names the game's
 * address. Only what one car on the track needs is here: not the other cars (the solver 0x245a3, the cars'
 * outlines 0x22d35), the driver, laps and places (race.c has those: the race is four of these cars, the three computer
 * cars with `ai` set and driven through s2car_step_driven()), cones, sparks, smoke, sound; the car's picture only as far
 * as the wheels' turn and steering (0x16878).
 *
 * PRECISION. The game's routines run on the x87 set to 53 bits: every sum is one rounding to a double and a
 * float is stored only where the game stores a DWORD. The Pocket's CPU has float32 only, so every sum here is
 * rounded to a float, in the game's order where it is known. Where the game cuts a number to a whole number
 * (the car's place each frame, the wheels' heights) that stays; the cut-off comes back as speed, so the
 * car's slow creep sideways on the grid is the game's (S2-5l) but not bit for bit. The easing tables are those
 * of a 50 frames a second step: (0.5 + 0.005 i). Nothing needs double.
 *
 * THE CAR'S PLACE. The game keeps x and z of the car's middle as whole numbers, the fraction cut off towards the
 * map's origin every frame (0x1c7f8, 0x1e529; the height too). The game ran at 10 to 20 frames a second, where that
 * loses little; at 50 a second it eats up to a unit a frame, which is enough to stop a car that heads away from the
 * origin (docs/screamer2/driving.md). Here the place is a float (words 0x4d8 and 0x4e0 hold floats, and so do their
 * copies at 0x734), kept to the fraction. -DS2_WHOLE_PLACES=1 cuts it as the game does: that is how the host test
 * (tools/s2/check_car.sh) follows the reference traces step for step. The heights stay whole numbers.
 */
#include "s2car.h"                            /* (S2_WHOLE_PLACES, s2car_placed) */
#include "s2params.h"
#include "s2env.h"
#include "track.h"
#include "car.h"
#include "dat.h"
#include "prof.h"
#include <math.h>
#include <string.h>

#ifdef __riscv
#include "files.h"
#else
#include <stdio.h>
#include <stdlib.h>
#endif

/* every product and sum is rounded to a float on its own, as the reference's stores are: no fused multiply-add */
#pragma GCC optimize ("-ffp-contract=off")

#define PI_F        3.14159274f
#define TWO_PI_F    6.28318548f
#define HALF_PI_F   1.57079637f
#define DT          0.02f                   /* the step; the game's raw frame time is the same */
static float step_dt = DT;                  /* the step the passes before the touching cars' solver work with (the game's 0x133350): DT, or
                                               what is left of it after a touch (s2car_move_again) */

void (*s2car_sparks)(const float at[3], const float dir[3], float hit, int set);

union word { int32_t i; uint32_t u; float f; };
typedef struct s2car_state car_t;
#define W(o)    (((union word *)c->rec)[(o) >> 2])
#define RF(o)   W(o).f
#define RI(o)   W(o).i
#define WH(i)   (0x574 + 0x70 * (i))        /* wheel i's record: +0 speed up, +4 on the ground, +8 over ground, +0x10 turn,
                                               +0x14 spring now, +0x18 at rest, +0x1c, +0x20 travel, +0x28 easing, +0x2c x,
                                               +0x30 height, +0x34 z, +0x38 .. +0x40 its place in the car, +0x44 the ground's
                                               height, +0x48 bump, +0x5c road kind, +0x60 skid, +0x64 .. +0x6c slope */
#define CORNER(i) (0x514 + 0xc * (i))       /* the car's corner i: x, height, z as floats */
#define PUSH(i)   (0x544 + 0xc * (i))       /* the wheel's push (speed of the corner) */
#define KINDS     64

#include "trigmemo.h"

/* ----------------------------------------------------------------------------------------- the tables */

static struct {
    struct s2_car_params cars[S2_CARS_TOTAL];
    int cars_ok;
    struct s2_track_params trk;
    int track;                              /* of trk, 0: none */
    struct s2_road_kind kind[KINDS];        /* by id; a row of zeros for any other */
    int start_x, start_z;                   /* the car's line's first node: MAPnOPP.SPL */
    uint16_t dir_cell[2048], wall_cell[1024];
    int dir_sorted, wall_sorted;
    const struct track_dir *dirs;
    const struct track_wall *walls;
    int ndirs, nwalls;
    float road_heading;                     /* 0x11cddc: the last road point found */
    int road_found;
    const struct track_wall *wall;          /* 0x11cdd8: the last wall found */
    uint8_t game_buf[0x2000 + 8], map_buf[0x6000 + 8], text[0x3000];
    const uint8_t *game_dat, *map_dat;      /* the containers, handed over or read */
    uint32_t game_size, map_size;
    int loaded_track;
} G;

static const int tyre_by_track[7] = { 1, 1, 2, 0, 2, 0, 0 };    /* the track's own tyres (14e20): 0 slick 1 rain 2 snow */
static const int tyre_factor[3] = { 0, 3, 6 };                    /* 0x112e38 */
static const float gear_ratio[6] = { 2.5f, 2.5f, 1.6f, 1.2f, 1.0f, 0.0f };   /* 0x112e24 */
static const int rough_mask[5] = { 0, 15, 31, 63, 127 };          /* 0x112e50 */

void s2car_set_data(const uint8_t *game_dat, uint32_t game_size, const uint8_t *map_dat, uint32_t map_size)
{
    G.game_dat = game_dat;
    G.game_size = game_size;
    G.map_dat = map_dat;
    G.map_size = map_size;
    G.cars_ok = 0;
    G.loaded_track = 0;
}

/* the road kind row of an id (zeros for any the track does not list) */
static const struct s2_road_kind *kind_of(int id) { return &G.kind[id >= 0 && id < KINDS ? id : 0]; }

static int cell_of(int x, int y) { return ((y + 0x100000) >> 13) * 256 + ((x + 0x100000) >> 13); }

/* the cell numbers of the road points and walls, which lie in the file's order (cells row by row), and
   whether they are in ascending order, which the lookup needs */
static void index_track(void)
{
    G.dirs = track_dirs(&G.ndirs);
    G.walls = track_walls(&G.nwalls);
    if(G.ndirs > 2048) G.ndirs = 0;
    if(G.nwalls > 1024) G.nwalls = 0;
    G.dir_sorted = G.wall_sorted = 1;
    for(int i = 0; i < G.ndirs; i++) {
        G.dir_cell[i] = (uint16_t)cell_of(G.dirs[i].x, G.dirs[i].y);
        if(i && G.dir_cell[i] < G.dir_cell[i - 1]) G.dir_sorted = 0;
    }
    for(int i = 0; i < G.nwalls; i++) {
        G.wall_cell[i] = (uint16_t)cell_of(G.walls[i].x, G.walls[i].y);
        if(i && G.wall_cell[i] < G.wall_cell[i - 1]) G.wall_sorted = 0;
    }
}

/* 0x2a371: the nearest road point (walls 0) or wall segment (1) to (x, y) among the nine grid cells round
   it, rows then columns, each cell's list in stored order; the first of equal distances (squares of whole
   numbers, wrapping); -1 if there is none. The cells' numbers ascend in the file, so each row's three cells
   are one run of the list. */
static int nearest(int walls, int x, int y)
{
    const uint16_t *cells = walls ? G.wall_cell : G.dir_cell;
    const int n = walls ? G.nwalls : G.ndirs, sorted = walls ? G.wall_sorted : G.dir_sorted;
    if(!n) return -1;
    const int32_t *px = walls ? &G.walls[0].x : &G.dirs[0].x;
    const int stride = walls ? 7 : 3;               /* int32 words between records */
    int cx = (x + 0x100000) >> 13, cy = (y + 0x100000) >> 13;
    int32_t least = 0x7ffffff8;
    int found = -1;
    for(int row = cy - 1; row <= cy + 1; row++) {
        if(row < 0 || row > 255) continue;
        int lo = row * 256 + (cx - 1 < 0 ? 0 : cx - 1), hi = row * 256 + (cx + 1 > 255 ? 255 : cx + 1);
        if(cx - 1 > 255 || cx + 1 < 0) continue;
        int i = 0;
        if(sorted) {                                /* the first record whose cell is not below lo */
            int a = 0, b = n;
            while(a < b) {
                int m = (a + b) / 2;
                if(cells[m] < lo) a = m + 1; else b = m;
            }
            i = a;
        }
        for(; i < n; i++) {
            if(sorted ? cells[i] > hi : (cells[i] < lo || cells[i] > hi)) { if(sorted) break; else continue; }
            int32_t dx = px[stride * i] - x, dy = px[stride * i + 1] - y;
            int32_t d = (int32_t)((uint32_t)dx * (uint32_t)dx + (uint32_t)dy * (uint32_t)dy);
            if(d < least) { least = d; found = i; }
        }
    }
    return found;
}

/* the dir of 0x2a371(0): the road's heading at a place, kept as the game does when nothing is found */
static void find_road(int x, int y)
{
    int i = nearest(0, x, y);
    if(i < 0) return;
    float a = G.dirs[i].angle;
    if(a > TWO_PI_F) a += -TWO_PI_F;
    G.road_heading = a;
    G.road_found = 1;
}

/* ------------------------------------------------------------------------------------------ small helpers */

/* a float cut to a whole number towards zero (the game's fistp in chop mode), without the undefined case */
static int32_t cut(float f)
{
    if(!(f > -2147483648.0f)) return INT32_MIN;
    if(f >= 2147483648.0f) return INT32_MAX;
    return (int32_t)f;
}

static float easing(int i) { return 0.5f + (float)(i < 0 ? 0 : i > 99 ? 99 : i) * 0.005f; }

/* 0x111d6: a float eased towards a target by the row of the easing table; 0x11212 the same for a whole number */
static void ease(float *v, float target, int i) { *v = (*v - target) * easing(i) + target; }
static void ease_int(int32_t *v, float target, int i) { *v = cut(((float)*v - target) * easing(i) + target); }

/* 0x14dc0: a - b brought into -pi .. pi */
static float ang_diff(float a, float b)
{
    float d = a - b;
    while(d > PI_F) d += -TWO_PI_F;
    while(d < -PI_F) d += TWO_PI_F;
    return d;
}

/* 0x1016b: an angle eased the short way round, kept in 0 .. 2 pi */
static void ease_angle(float *v, float target, int i)
{
    float row = easing(i), a = *v;
    if(a != target) {
        if(a < target) {
            if(target - a >= PI_F) *v = -((target - a) + -TWO_PI_F) * row + target;
            else *v = (a - target) * row + target;
        } else {
            if(a - target >= PI_F) *v = -((target - a) + TWO_PI_F) * row + target;
            else *v = (a - target) * row + target;
        }
    }
    if(*v < 0.0f) *v += TWO_PI_F;
    else if(*v >= TWO_PI_F) *v += -TWO_PI_F;
}

/* an angle on the ground, the game's way: of a vector's x and z */
static float ground_angle(float x, float z)
{
    float a = z != 0.0f ? atanf(fabsf(x / z)) : HALF_PI_F;
    if(z > 0.0f) a = PI_F - a;
    if(x < 0.0f) a = TWO_PI_F - a;
    return a;
}

static float len3(float a, float b, float c) { return sqrtf(b * b + a * a + c * c); }

/* 0x10388: a point (x, y, z whole numbers) turned about the up axis, cut */
static void turn_up(int32_t out[3], const int32_t in[3], float angle)
{
    float c = cosf(angle), s = sinf(angle), x = (float)in[0], z = (float)in[2];
    out[0] = cut(z * s + x * c);
    out[1] = in[1];
    out[2] = cut(z * c - x * s);
}

/* 0x105cd's relative: a point turned by roll, pitch, turn, each step's sine and cosine floats (0x1d9fc) */
static void turn3(float v[3], float roll, float pitch, float turn)
{
    float s = sinf(roll), co = cosf(roll);
    float x = v[0] * co - v[1] * s, y = v[1] * co + v[0] * s;
    v[0] = x; v[1] = y;
    s = sinf(pitch); co = cosf(pitch);
    y = v[1] * co + v[2] * s;
    float z = v[2] * co - v[1] * s;
    v[1] = y; v[2] = z;
    s = sinf(turn); co = cosf(turn);
    x = v[0] * co - v[2] * s;
    z = v[2] * co + v[0] * s;
    v[0] = x; v[2] = z;
}

/* ----------------------------------------------------------------------------------------- the routines */

/* 0x1d9fc: the car's four corners from its middle (whole numbers) and angles; the wheels take their x and z */
static void corners(car_t *c)
{
    float ahead[3] = { 0.0f, 0.0f, -1.0f }, side[3] = { -1.0f, 0.0f, 0.0f };
    turn3(ahead, -RF(0x4ec), RF(0x4e4), RF(0x4e8));
    float half = (float)RI(0x4d0) * 0.5f;
    for(int k = 0; k < 3; k++) ahead[k] *= half;
    turn3(side, -RF(0x4ec), RF(0x4e4), RF(0x4e8));
    float width = (float)RI(0x4c8) * 0.5f;
    for(int k = 0; k < 3; k++) side[k] *= width;
    static const uint16_t at[4] = { 0x52c, 0x514, 0x538, 0x520 };
    static const int sa[4] = { 1, 1, -1, -1 }, ss[4] = { 1, -1, 1, -1 };
    for(int i = 0; i < 4; i++) {
        int o = at[i];
        /* the game adds the middle (about 1e5) and the two offsets (about 4e2) in 64 bits and rounds once; the
           offsets first, then the big one, rounds once as well (the float has a 128th of a unit left at 1e5) */
        RF(o) = RF(0x4d8) + ((float)sa[i] * ahead[0] + (float)ss[i] * side[0]);
        RF(o + 8) = RF(0x4e0) + ((float)sa[i] * ahead[2] + (float)ss[i] * side[2]);
        RF(o + 4) = (float)RI(0x4dc) + ((float)sa[i] * ahead[1] + (float)ss[i] * side[1]);
    }
    for(int i = 0; i < 4; i++) {
        RI(0x5a0 + i * 0x70) = cut(RF(CORNER(i)));
        RI(0x5a8 + i * 0x70) = cut(RF(CORNER(i) + 8));
    }
}

/* 0x1c7f8: the car's angles from its corners */
static void hold_angle(car_t *c, int at)
{
    if(fabsf(RF(at)) > 1.41371673f) RF(at) = (RF(at) < 0.0f ? -1.0f : 1.0f) * PI_F * 0.45f;
    if(fabsf(RF(at)) > 0.942477822f) ease_angle(&RF(at), (RF(at) < 0.0f ? -1.0f : 1.0f) * PI_F * 0.3f, 5);
}

static void angles_from_corners(car_t *c)
{
    float A[3], B[3];
    for(int k = 0; k < 3; k++) {
        float l = RF(CORNER(0) + 4 * k) - RF(CORNER(1) + 4 * k), r = RF(CORNER(2) + 4 * k) - RF(CORNER(3) + 4 * k);
        A[k] = (r + l) * 0.5f;
    }
    for(int k = 0; k < 3; k++) {
        float b = RF(CORNER(3) + 4 * k) - RF(CORNER(1) + 4 * k), f = RF(CORNER(2) + 4 * k) - RF(CORNER(0) + 4 * k);
        B[k] = (f + b) * 0.5f;
    }
    RF(0x4e8) = ground_angle(A[0], A[2]);
    float a = -RF(0x4e8), sn = sinf(a), cs = cosf(a);
    float az = A[2] * cs + A[0] * sn;
    RF(0x4e4) = ground_angle(A[1], az);
    float bx = B[0] * cs - B[2] * sn, bz = B[2] * cs + B[0] * sn;
    a = -RF(0x4e4); sn = sinf(a); cs = cosf(a);
    float by = B[1] * cs + bz * sn;
    RF(0x4ec) = -ground_angle(by, bx);
    RF(0x4e4) = ang_diff(RF(0x4e4), 0.0f);
    RF(0x4e8) = ang_diff(RF(0x4e8), 0.0f);
    RF(0x4ec) = ang_diff(RF(0x4ec), 0.0f);
    hold_angle(c, 0x4e4);
    hold_angle(c, 0x4ec);
    float m[3];
    for(int k = 0; k < 3; k++) {
        if(S2_WHOLE_PLACES)                             /* the game's order of sums */
            m[k] = (((RF(CORNER(2) + 4 * k) + RF(CORNER(0) + 4 * k)) + RF(CORNER(3) + 4 * k)) + RF(CORNER(1) + 4 * k)) * 0.25f;
        else                                            /* the corners' offsets from one of them first: one rounding of the big number */
            m[k] = RF(CORNER(0) + 4 * k) + (((RF(CORNER(2) + 4 * k) - RF(CORNER(0) + 4 * k)) + (RF(CORNER(3) + 4 * k) - RF(CORNER(0) + 4 * k)))
                                            + (RF(CORNER(1) + 4 * k) - RF(CORNER(0) + 4 * k))) * 0.25f;
    }
    RF(0x4d8) = s2car_placed(m[0]);
    RF(0x4e0) = s2car_placed(m[2]);
    for(int i = 0; i < 4; i++) RF(0x518 + 0xc * i) = (float)RI(0x5a4 + 0x70 * i);
    RF(0xc14) = atanf((float)(RI(0x5b8) - RI(0x698)) / (float)RI(0x4c8));
    RF(0xc0c) = atanf((float)(RI(0x698) - RI(0x708)) / (float)RI(0x4d0));
}

/* what is under the four corners: one look at each serves the walls' question (is there ground at all) and the
   wheels' (its height, slope and kind) -- the game asks both at the same places */
struct corner_ground { int found, held, h, n[3], kind; };
static void look_under_corners(car_t *c, struct corner_ground q[4])
{
    for(int i = 0; i < 4; i++) {
        q[i].h = 0; q[i].n[0] = q[i].n[1] = q[i].n[2] = 0; q[i].kind = 0;
        q[i].found = track_ground_s2(cut(RF(CORNER(i))), cut(RF(CORNER(i) + 8)), &q[i].held, &q[i].h, q[i].n, &q[i].kind);
    }
}

/* 0x28f96: the ground under the four wheels */
static void ground_under_wheels(car_t *c, const struct corner_ground q[4])
{
    float sum = 0.0f;
    int found = 0;
    for(int i = 0; i < 4; i++) {
        int w = WH(i), h = q[i].h, kind = q[i].kind;
        const int *n = q[i].n;
        if(!q[i].found) { RI(w + 8) = 0; continue; }
        RF(w + 0x64) = (float)n[0]; RF(w + 0x68) = (float)n[1]; RF(w + 0x6c) = (float)n[2];
        RI(w + 0x44) = h;
        sum = (float)RI(w + 0x44) + sum;
        RI(w + 0x5c) = kind;
        RI(w + 8) = 1;
        found++;
    }
#define ON(j)       RI(WH(j) + 8)
#define HEIGHT(j)   RI(WH(j) + 0x44)
#define CX(j)       RF(CORNER(j))
#define CZ(j)       RF(CORNER(j) + 8)
    if(found == 1 || found == 2) {
        float mean = sum / (float)found;
        for(int j = 0; j < 4; j++) if(!ON(j)) { ON(j) = 1; HEIGHT(j) = cut(mean); }
        return;
    }
    if(found != 3) return;
    float n[3] = { 0.0f, 0.0f, 0.0f };
    int i = 0;
    for(i = 0; i < 4; i++) {
        if(!ON(i)) continue;
        int done = 0;
        for(int j = i + 1; j < 4 && !done; j++) {
            if(!ON(j)) continue;
            for(int k = j + 1; k < 4; k++) {
                if(!ON(k)) continue;
                float a = CX(j) - CX(i), cc = CZ(j) - CZ(i), b = (float)(HEIGHT(j) - HEIGHT(i));
                float d = CX(k) - CX(i), f = CZ(k) - CZ(i), e = (float)(HEIGHT(k) - HEIGHT(i));
                n[0] = b * f - cc * e;
                n[1] = cc * d - a * f;
                n[2] = a * e - b * d;
                break;
            }
            done = 1;
        }
        break;
    }
    for(int m = 0; m < 4; m++) {
        if(ON(m)) continue;
        float len = len3(n[0], n[1], n[2]);
        if(len == 0.0f) n[0] = n[1] = n[2] = 0.0f;
        else { float inv = 1.0f / len; for(int k = 0; k < 3; k++) n[k] *= inv; }
        float dx = CX(m) - CX(i), dz = CZ(m) - CZ(i);
        HEIGHT(m) = cut((float)HEIGHT(i) - (dz * n[2] + dx * n[0]) / n[1]);
        ON(m) = 1;
        break;
    }
#undef ON
#undef HEIGHT
#undef CX
#undef CZ
}

/* 0x19aaa: a knock: the side's count for the look and, once it is 1, a knock over 2000 puts the side's mark
   in the car's flags (+0xb8: what the drawn car's textures show), and with DAMAGES on (s2car_damages: the game has it off
   until its menu says so) the corner's and the car's damage, and the corner's spring softer. The force the damage goes by is scaled
   with the level's row of MISC.DTR's DAMAGES (pro 5, normal 2.5, rookie 1) for every knock, a wall's and a car's alike and the player's and the
   computers' (0x19b7b: force x [0x11cce0 + 4 x [0x130d4c]], after the side's mark, which goes by the force as it came): s2car_damage_scale, which race_init()
   sets from the level (1 until it does: the tools and the tests). */
int s2car_damages = 1;
float s2car_damage_scale = 1.0f;

static void knock(car_t *c, int side, int corner, float share, float force)
{
    if(!s2car_damages) return;
    static const uint32_t mark[4] = { 0x2000, 0x800, 0x4000, 0x1000 };       /* sides 0 .. 3 (the table at 0x19b58) */
    int count = 0x1c + 4 * side;
    RF(count) = (1.0f - share) * force / 20000.0f + RF(count);
    if(RF(count) >= 1.0f) {
        RF(count) = 1.0f;
        if(force > 2000.0f) RI(0xb8) |= (int32_t)mark[side];
    }
    force = force * s2car_damage_scale;
    int hurt = 0xc + 4 * corner;
    RF(hurt) = share * force / 50000.0f + RF(hurt);
    if(RF(hurt) > 2.0f) RF(hurt) = 2.0f;
    if(corner == 2 || corner == 0) RF(8) = share * force / 100000.0f + RF(8);
    if(side == 1) RF(8) = (1.0f - share) * force / 100000.0f + RF(8);
    if(RF(8) > 2.0f) RF(8) = 2.0f;
    RI(0x59c + corner * 0x70) = (int32_t)((1.0f - RF(hurt)) * 55.0f + RF(hurt) * 99.0f);
}

/* 0x20970: where two lines meet: the car's line (point p, direction d) and the wall's (q, e) on the ground */
static int lines_meet(const float p[3], const float d[3], const float q[3], const float e[3], float *s, float *u)
{
    float n = e[2] * d[0] - d[2] * e[0];
    if(n == 0.0f) return 0;
    float a = q[0] - p[0], b = p[2] - q[2];
    *s = (a * e[2] + b * e[0]) / n;
    *u = (a * d[2] + b * d[0]) / n;
    return 1;
}

/* 0x29571: the car against a wall. A corner off the drivable ground is off (+0x170 + 0x20 i); the nearest
   wall gives the way out (the corner turned into the wall's frame, put 2 units out of it, and turned back);
   the biggest move is taken; the car is moved, spins towards the wall's way and is knocked. */
static void against_wall(car_t *c, const struct corner_ground q[4])
{
    float hard = 0.0f;
    int hit = 0;
    RI(0x150) = 0;
    for(int i = 0; i < 4; i++) {
        RI(0x170 + 0x20 * i) = !q[i].held;
        if(RI(0x170 + 0x20 * i)) RI(0x150) = 1;
    }
    if(RI(0x150) == 0) return;
    for(int i = 0; i < 4; i++) {
        int row = 0x164 + 0x20 * i;
        if(RI(row + 0xc) == 0) { RI(row + 0x18) = 0; RI(row + 0x10) = 0; RI(row + 0x1c) = 0; continue; }
        int x = cut(RF(CORNER(i))), z = cut(RF(CORNER(i) + 8));
        int wi = nearest(1, x, z);
        if(wi < 0) continue;
        G.wall = &G.walls[wi];
        RF(row + 0x1c) = ang_diff(PI_F, G.wall->angle);
        int32_t v[3] = { x - G.wall->x, 0, z - G.wall->y }, o1[3], o2[3];
        turn_up(o1, v, RF(row + 0x1c));
        o1[0] = 2;
        turn_up(o2, o1, -RF(row + 0x1c));
        RI(row + 0x10) = o2[0] - v[0];
        RI(row + 0x18) = o2[2] - v[2];
    }
    RI(0x15c) = 0;
    RI(0x154) = 0;
    for(int i = 0; i < 4; i++) {
        int row = 0x164 + 0x20 * i;
        if(RI(row + 0xc) == 0) continue;
        int32_t a = RI(0x154) < 0 ? -RI(0x154) : RI(0x154), b = RI(0x15c) < 0 ? -RI(0x15c) : RI(0x15c);
        int32_t a2 = RI(row + 0x10) < 0 ? -RI(row + 0x10) : RI(row + 0x10), b2 = RI(row + 0x18) < 0 ? -RI(row + 0x18) : RI(row + 0x18);
        if(a + b <= a2 + b2) {
            RI(0x154) = RI(row + 0x10);
            RI(0x15c) = RI(row + 0x18);
            RF(0x160) = RF(row + 0x1c);
            hit = i;
        }
    }
    if(!G.wall) { RI(0x150) = 0; return; }
    for(int i = 0; i < 4; i++) {
        if(RI(0x170 + 0x20 * i) != 0) continue;
        float mx = (float)RI(0x154), mz = (float)RI(0x15c);
        float wx = (float)G.wall->x - RF(CORNER(i)), wz = (float)G.wall->y - RF(CORNER(i) + 8);
        float to_wall = ground_angle(wx, wz), move = ground_angle(mx, mz);
        if(!(fabsf(ang_diff(move, to_wall)) >= 1.5707963705f)) { RI(0x150) = 0; return; }
        break;
    }
    RF(0x4d8) += (float)RI(0x154);
    RF(0x4e0) += (float)RI(0x15c);
    if(fabsf(ang_diff(RF(0x160), RF(0x4e8))) > 1.5707963705f) RF(0x160) = ang_diff(RF(0x160), PI_F);
    int push = PUSH(hit);
    if(s2car_sparks) {                              /* 0x2aeb9 (the wall's thirty): at the corner, thrown along (0, the push's up and z parts: the game's x is left 0), the car's speed in units a second */
        const float at[3] = { RF(CORNER(hit)), RF(CORNER(hit) + 4), RF(CORNER(hit) + 8) }, dir[3] = { 0.0f, RF(push + 4), RF(push + 8) };
        s2car_sparks(at, dir, RF(0x500) * 400000.0f / 3600.0f, 0);
    }
    float P[3] = { RF(0x4d8), 0.0f, RF(0x4e0) };
    float D[3] = { sinf(RF(0x4e8)), 0.0f, -cosf(RF(0x4e8)) };
    float Q[3] = { (float)G.wall->x, (float)RI(0x4dc), (float)G.wall->y };
    float E[3] = { sinf(RF(0x160)), 0.0f, -cosf(RF(0x160)) };
    float T[2];
    if(!(fabsf(RF(0x4d4)) * RF(0x500) / RF(0x5c) >= 0.6283185482f)) {
        if(!lines_meet(P, D, Q, E, &T[0], &T[1])) {
            RF(0x54) = RF(0x160);
            RI(0x58) = ang_diff(RF(0x54), RF(0x4e8)) < 0.0f ? -1 : 1;
        } else {
            if(T[0] > 0.0f) {
                float a;
                if(RF(0x500) < 30.0f && c->ai) {                /* a computer's car under speed 30: out along the wall's way */
                    a = fabsf(ang_diff(RF(0x160), RF(0x4c4))) > 1.570796370506286f ? ang_diff(RF(0x160), PI_F) : RF(0x160);
                } else if(RF(0x500) >= 20.0f || !(fabsf(RF(0x660)) > RF(0x6c) * 0.2f)) {
                    float off = fabsf(ang_diff(RF(0x4e8), RF(0x4c4)));
                    if(!((HALF_PI_F - off) * RF(0x1f0) < 0.0f)) a = RF(0x160);
                    else a = fabsf(ang_diff(RF(0x4c4), RF(0x160))) > 1.5707963705f ? ang_diff(RF(0x160), -PI_F) : RF(0x160);
                } else {
                    int sg = RF(0x660) < 0.0f ? -1 : 1;
                    float r = ang_diff((float)sg * HALF_PI_F + RF(0x4e8), RF(0x160));
                    a = fabsf(r) > 1.5707963705f ? ang_diff(RF(0x160), -PI_F) : RF(0x160);
                }
                RI(0x58) = ang_diff(a, RF(0x4e8)) < 0.0f ? -1 : 1;
                RF(0x54) = (float)RI(0x58) * PI_F * 0.06f + a;
            }
            float mx = (float)RI(0x154), mz = (float)RI(0x15c);
            float side = ang_diff(ground_angle(mx, mz) + PI_F, RF(0x4e8));
            float across = fabsf(sinf(ang_diff(ground_angle(RF(push), RF(push + 8)), RF(0x160))));
            float len = len3(RF(push), RF(push + 4), RF(push + 8));
            hard = len * across;
            if(!(fabsf(side) >= 0.7853981852f)) knock(c, 1, hit, fabsf(side) / 0.7853981852f, hard);
            else if(fabsf(side) > 2.35619455575943f) knock(c, 3, hit, fabsf(PI_F - side) / 0.7853981852f, hard);
            else if(side < 0.0f) knock(c, 0, hit, fabsf(-side + -HALF_PI_F) / 0.7853981852f, hard);
            else knock(c, 2, hit, fabsf(side + -HALF_PI_F) / 0.7853981852f, hard);
        }
    }
    if(hard > c->hit_hard) c->hit_hard = hard;
    float into = -cosf(RF(0x508)) * (float)RI(0x15c) + sinf(RF(0x508)) * (float)RI(0x154);
    if(into <= 0.0f) {
        float mean = (((RF(0x574) + RF(0x5e4)) + RF(0x654)) + RF(0x6c4)) * 0.25f;
        for(int i = 0; i < 4; i++) RF(0x574 + 0x70 * i) = mean;
    } else RI(0x150) = 0;
    RF(0x30) = 1.0f;
    RF(0x34) = 0.3f;
}

/* 0x1bef1: the springs and the wheels' heights */
static void springs(car_t *c)
{
    RI(0x4f4) = 0; RI(0x4dc) = 0; RI(0x510) = 0;
    const float dt = step_dt;
    float onground = 0.0f;
    for(int i = 0; i < 4; i++) {
        int w = WH(i), corner = 0x518 + 0xc * i;
        int32_t was = RI(w + 0x30), bump = 0;
        if(RI(w + 8) == 0) RI(w + 0x48) = 0;
        else {
            int32_t b = RI(w + 0x2c) + RI(w + 0x34);
            int cls = kind_of(RI(w + 0x5c))->rough;
            int32_t rough = b & rough_mask[cls < 0 || cls > 4 ? 0 : cls];
            float v = (float)(rough * ((b & 2) ? -1 : 1));
            float f = RF(0x500) / RF(0x5c) * v;
            bump = cut(f);
            RI(w + 0x48) = cut((1.0f - RF(0x2c) / 10.0f) * (float)bump);
            bump -= RI(w + 0x48);
        }
        RI(w + 0x44) += bump;
        RF(w) = RF(w) - dt * 5886.0f;
        float low = ((float)RI(w + 0x44) + RF(w + 0x18)) - RF(w + 0x20);
        float y = RF(w) * dt + (float)RI(w + 0x30);
        RI(w + 0x30) = cut(y);
        if(!((float)RI(w + 0x30) >= low)) RI(w + 0x30) = cut(low);
        float high = ((float)RI(w + 0x44) + RF(w + 0x18)) + RF(w + 0x20) * 0.2f;
        if((float)RI(w + 0x30) >= high) {
            RI(w + 4) = 0;
            ease(&RF(w + 0x14), RF(w + 0x20) * 0.2f + RF(w + 0x18), 0x50);
        } else {
            RI(w + 4) = 1;
            if(!((float)(RI(w + 0x30) - RI(w + 0x44)) >= RF(w + 0x18))) {
                RF(w + 0x14) = (float)(RI(w + 0x30) - RI(w + 0x44));
                ease(&RF(w + 0x14), RF(w + 0x18), RI(w + 0x28));
                RI(w + 0x30) = cut((float)RI(w + 0x44) + RF(w + 0x14));
            } else {
                ease(&RF(w + 0x14), RF(w + 0x20) * 0.2f + RF(w + 0x18), 0x50);
                if(!((float)(RI(w + 0x30) - RI(w + 0x44)) >= RF(w + 0x14))) RF(w + 0x14) = (float)(RI(w + 0x30) - RI(w + 0x44));
            }
        }
        onground = (float)RI(w + 4) + onground;
        RF(0x510) = onground;
        int32_t half = (RI(w + 0x48) - (RI(w + 0x48) >> 31)) >> 1;
        float rise = (RF(corner) - (float)RI(w + 0x30)) + (float)half, held;
        if(-RF(w + 0x20) >= rise) held = -RF(w + 0x20);
        else if(rise >= RF(w + 0x20)) held = RF(w + 0x20);
        else held = rise;
        RI(w + 0x48) = cut(held);
        RF(corner) = (float)RI(w + 0x30);
        if(!(fabsf(RF(0x4d4)) >= 0.01f)) RF(corner) = (float)RI(w + 0x60) + RF(corner);
        float h = ((float)RI(w + 0x30) - RF(w + 0x14)) + (float)RI(0x4dc);
        RI(0x4dc) = cut(h);
        float sp = (float)RI(0x4f4) + RF(w + 0x14);
        RI(0x4f4) = cut(sp);
        if(!(RI(0x4c) & 8) && RI(0x150) == 0) {
            if(dt > 0.0f) RF(w) = (float)(RI(w + 0x30) - was) / dt;
            else RF(w) = 0.0f;
        } else if(!(RF(w) <= 0.0f)) RF(w) = 0.0f;
        if(RI(w + 8) != 0 && RI(0x7d8 + i * 0x70) != 1) RF(w) = 0.0f;
        RI(w + 0x44) -= bump;
    }
    RI(0x4f4) = cut((float)RI(0x4f4) * 0.25f);
    RF(0x510) = RF(0x510) * 0.25f;
    RI(0x4dc) = cut((float)RI(0x4dc) * 0.25f);
    RI(0x4f8) = cut((float)(RI(0x708) + (RI(0x5b8) + RI(0x698) + RI(0x628))) * 0.25f);
}

/* 0x1e529: the car's speed and way from where its middle went; the wheels' pushes; after a knock their cap */
static void speed_and_way(car_t *c)
{
    float dx = RF(0x4d8) - RF(0x734), dz = RF(0x4e0) - RF(0x73c), dy = (float)(RI(0x4dc) - RI(0x738));
    if(dx == 0.0f && dz == 0.0f) RF(0x508) = RF(0x4e8);
    else RF(0x508) = ground_angle(dx, dz);
    const float dt = step_dt;
    RF(0x500) = len3(dx, dy, dz) / dt * 3600.0f / 400000.0f;
    float best[3] = { 0.0f, 0.0f, 0.0f };
    for(int i = 0; i < 4; i++) {
        int push = PUSH(i), corner = CORNER(i), was = 0x770 + 0xc * i;
        float v[3];
        for(int k = 0; k < 3; k++) v[k] = RF(corner + 4 * k) - RF(was + 4 * k);
        for(int k = 0; k < 3; k++) RF(push + 4 * k) = v[k] / dt;
        if(RF(0x34) > 0.0f) {
            int k7 = 0x7a0 + 0xc * i;
            float own = len3(RF(k7), RF(k7 + 4), RF(k7 + 8));
            float cap = own > 3330.0f ? own : 3330.0f;
            float m = len3(RF(push), RF(push + 4), RF(push + 8));
            if(m > cap) {
                float u[3] = { 0.0f, 0.0f, 0.0f };
                if(m != 0.0f) { float inv = 1.0f / m; for(int k = 0; k < 3; k++) u[k] = RF(push + 4 * k) * inv; }
                for(int k = 0; k < 3; k++) RF(push + 4 * k) = u[k] * cap;
                float old[3] = { RF(corner), RF(corner + 4), RF(corner + 8) }, d[3];
                for(int k = 0; k < 3; k++) RF(corner + 4 * k) = RF(was + 4 * k) + RF(push + 4 * k) * dt;
                for(int k = 0; k < 3; k++) d[k] = RF(corner + 4 * k) - old[k];
                if(len3(best[0], best[1], best[2]) < len3(d[0], d[1], d[2])) { best[0] = d[0]; best[1] = d[1]; best[2] = d[2]; }
            }
        }
    }
    RI(0x34) = 0;
    RF(0x4d8) = s2car_placed(RF(0x4d8) + best[0]);
    RF(0x4e0) = s2car_placed(RF(0x4e0) + best[2]);
    float h = -sqrtf(dz * dz + dx * dx);
    if(h == 0.0f) RI(0x50c) = 0;
    else RF(0x50c) = ground_angle(dy, h);
    RF(0x44) = ang_diff(RF(0x4e8), RF(0x744)) / dt;
    RF(0x4d4) = ang_diff(RF(0x508), RF(0x4e8));
}

/* 0x19810: the wheels' pushes cut to the part along the ground's direction at the angle, over half the top speed */
static void push_sideways(car_t *c, float angle)
{
    angle += 1.5707964f;
    float cs = sinf(angle + 1.5707964f), sn = -cosf(angle + 1.5707964f);
    for(int i = 0; i < 4; i++) {
        int w = PUSH(i);
        float along = RF(w) * cs + RF(w + 8) * sn;
        float k = RF(0x500) / RF(0x5c) + -0.5f;
        if(k > 0.0f) {
            float m = k * 0.8f + 0.6f;
            RF(w) = (cs * along) * m; RF(w + 4) = 0.0f; RF(w + 8) = (sn * along) * m;
        }
    }
}

/* 0x1c4fb: the body on its springs */
static void body_lean(car_t *c)
{
    if((RI(0x4c) & 8) || RI(0x150)) { RI(0xc18) = 0; RI(0xc20) = 0; return; }
    float pitch = (0.12f / RF(0x674) * cosf(RF(0x4d4)) * (RF(0x75c) - RF(0x500))) / step_dt;
    float roll = -(0.12f / RF(0x674)) * sinf(RF(0x4d4)) * RF(0x500);
    float w[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    w[2] = w[2] - pitch; w[0] = w[2];
    w[3] = w[3] + pitch; w[1] = w[3];
    w[0] = w[0] + roll; w[1] = w[1] + roll;
    w[2] = w[2] - roll; w[3] = w[3] - roll;
    for(int i = 0; i < 4; i++) {
        int sp = WH(i);
        if(w[i] == 1.0f) continue;
        float by = fabsf(1.0f - w[i]) / 0.6f * RF(sp + 0x20);
        RF(sp + 0x1c) = w[i] < 1.0f ? RF(sp + 0x18) - by : RF(sp + 0x18) + by;
    }
    for(int i = 0; i < 4; i++) {
        int sp = WH(i);
        float d = RF(sp + 0x1c) - RF(sp + 0x18);
        if(RF(sp + 0x20) >= fabsf(d)) w[i] = RF(sp + 0x1c);
        else w[i] = (d < 0.0f ? -1.0f : 1.0f) * RF(sp + 0x20) + RF(sp + 0x18);
    }
    int rate = cut(RF(0x30) * 68.0f + 30.0f);
    float t = ((w[2] - w[3]) + (w[0] - w[1])) * 0.5f;
    ease(&RF(0xc18), atanf(t / (float)RI(0x4d0)) * 2.0f, rate);
    t = ((w[1] - w[3]) + (w[0] - w[2])) * 0.5f;
    ease(&RF(0xc20), atanf(t / (float)RI(0x4c8)) * 2.0f, rate);
}

/* 0x19cac: one tyre on the ground (wheel 0 and 2 the front, turned to angle); gives how far the speed along was off */
static float tyre(car_t *c, int wheel, float angle)
{
    float s = sinf(angle), co = -cosf(angle), y0 = 0.0f;
    int push = PUSH(wheel), w = WH(wheel);
    const float dt = step_dt;
    float V[3] = { RF(push), RF(push + 4), RF(push + 8) }, N[3] = { 0.0f, 0.0f, 0.0f };
    {
        float len = len3(RF(w + 0x64), RF(w + 0x68), RF(w + 0x6c));
        if(len != 0.0f) { float inv = 1.0f / len; for(int k = 0; k < 3; k++) N[k] = RF(w + 0x64 + 4 * k) * inv; }
    }
    V[0] = ((((fabsf(s) * N[0]) * 0.8f) * 9.81f) * 400.0f) * dt + V[0];
    V[2] = ((((fabsf(co) * N[2]) * 0.8f) * 9.81f) * 400.0f) * dt + V[2];
    float A[3] = { 0.0f, 0.0f, 0.0f }, B[3] = { 0.0f, 0.0f, 0.0f };
    if(len3(V[0], V[1], V[2]) > 0.0f) {
        float dot = V[1] * y0 + V[0] * s + V[2] * co;
        A[0] = s * dot; A[1] = y0 * dot; A[2] = co * dot;
        B[0] = V[0] - A[0]; B[2] = V[2] - A[2];
    }
    float lenA = len3(A[0], A[1], A[2]);
    float top = RI(0xc7c) > 0 ? RF(0x4fc) * 111.0f : lenA;
    float rel = ang_diff(ground_angle(A[0], A[2]), RF(0x4e8));
    if(!(rel >= -HALF_PI_F) || rel > HALF_PI_F) lenA = -lenA;
    float thr = RI(0xc7c) == 0 ? RF(0x1e4) * 0.3f - RF(0xc0) : RF(0x1ec) * RF(0x1e4) * 0.3f - RF(0xc0);
    float hurt = RF(0xc + 4 * wheel) + -1.0f;
    float kn = hurt <= 0.0f ? 0.0f : hurt;
    float spring = (1.0f - kn * 0.2f) * RF(0x78 + 4 * wheel);
    float x = (wheel == 2 || wheel == 0) ? spring + thr : spring - thr;
    int tyre_set = RI(0x50);
    float ground = kind_of(RI(w + 0x5c))->grip[tyre_set < 0 || tyre_set > 2 ? 0 : tyre_set];
    float g = 1.0f - x * ground * RF(0x74) * dt;         /* 0x19c61: what is left of a speed after a frame's drag, not below 0 */
    if(g < 0.0f) g = 0.0f;
    if(lenA < 0.0f) ease(&lenA, 0.0f, 10);
    float v = (1.0f - g) * lenA + top * g;
    v = (1.0f - RF(0x1e8)) * v;
    float diff = fabsf(v - lenA);
    float lim = ground * 1500.0f * RF(0x74);
    lim = (1.0f + RF(0x1e8)) * lim;
    if(lim >= fabsf(v - lenA)) RI(w + 0x60) = 0;
    else {
        lim = lim * 0.5f;
        v = ((v - lenA) < 0.0f ? -1.0f : 1.0f) * lim + lenA;
        RI(w + 0x60) = cut(RF(CORNER(wheel)) + RF(CORNER(wheel) + 4)) & 0xf;
    }
    float r = dt * 5.0f * RF(0x88 + 4 * wheel);
    r = ((RF(0xc78) * 0.6f) / RF(0x60) + 0.7f) * r;
    lenA = r < 1.0f ? (v - lenA) * r + lenA : v;
    float nB[3];
    A[0] = s * lenA; A[1] = y0 * lenA; A[2] = co * lenA;
    for(int k = 0; k < 3; k++) nB[k] = B[k] * g;
    if(RI(0xc7c) != 0) {
        float m = len3(A[0], A[1], A[2]);
        if(m > 0.0f && 1.0f > fabsf(ang_diff(ground_angle(nB[0], nB[2]), ground_angle(B[0], B[2])))) {
            float d[3];
            for(int k = 0; k < 3; k++) d[k] = nB[k] - B[k];
            float ld = len3(d[0], d[1], d[2]);
            float q = RI(100) == 1 ? ld * 0.3f : ld * ((RF(0xc78) / RF(0x60)) * 0.3f + 0.15f);
            float la = len3(A[0], A[1], A[2]);
            float factor = q / la + 1.0f;
            for(int k = 0; k < 3; k++) A[k] *= factor;
        }
    }
    float om = 1.0f - RF(0x510);
    float Wv[3];
    for(int k = 0; k < 3; k++) Wv[k] = V[k] * om;
    for(int k = 0; k < 3; k++) A[k] = A[k] + nB[k];
    for(int k = 0; k < 3; k++) A[k] = A[k] * RF(0x510);
    for(int k = 0; k < 3; k++) A[k] = A[k] + Wv[k];
    if(!(RF(0x1ec) >= 0.1f) || RI(0xc7c) == 0) {
        float len = len3(A[0], A[1], A[2]);
        if(len < 300.0f) A[0] = A[1] = A[2] = 0.0f;
        else if(len < 1000.0f) {
            float f = !(1.0f >= dt * 2.0f) ? 1.0f : dt * 2.0f;
            float om2 = 1.0f - f;
            for(int k = 0; k < 3; k++) A[k] *= om2;
        }
    }
    for(int k = 0; k < 3; k++) RF(CORNER(wheel) + 4 * k) = RF(0x770 + 0xc * wheel + 4 * k) + A[k] * dt;
    return diff;
}

/* 0x1b36f: the tyres on the ground: the grip, the slide, the four tyres, the corners and the road's heading */
static void tyres(car_t *c, int *squeal)
{
    int tyre_set = RI(0x50);
    int ts = tyre_set < 0 || tyre_set > 2 ? 0 : tyre_set;
    const struct s2_road_kind *k0 = kind_of(RI(0x6b0)), *k1 = kind_of(RI(0x5d0)), *k2 = kind_of(RI(0x720)), *k3 = kind_of(RI(0x640));
    float base = (((k0->grip[ts] + k1->grip[ts]) + k2->grip[ts]) + k3->grip[ts]) * 0.25f;
    float rough = (float)(k0->rough + k1->rough + k2->rough + k3->rough) * 0.25f - (4.0f - RF(0x2c));
    float grip;                                         /* 0x1b36f: the roughness counts half as much for a computer's car */
    if(c->ai) grip = 0.0f >= rough ? rough * 0.05f + base : base - rough * 0.1f;
    else grip = 0.0f >= rough ? rough * 0.1f + base : base - rough * 0.2f;
    for(int i = 0; i < 0x25c / 4; i++) W(0x734 + 4 * i).u = W(0x4d8 + 4 * i).u;    /* (the last step's state, 604 bytes: copied by words, the library's copy goes by bytes) */
    int against = 0.0f > RF(0x4d4) * RF(0x660) || !(fabsf(RF(0x4d4)) >= 0.01f);
    if((against && fabsf(RF(0x660)) > RF(0x6c) * 0.1f) || RF(0x510) < 1.0f) RI(0xc0) = 0;
    else {
        RF(0xc0) = fabsf(cosf(RF(0x4d4)));
        RF(0xc0) = RF(0xc0) + -0.7f;
        if(0.0f >= RF(0xc0)) RI(0xc0) = 0;
        else {
            float ratio = RF(0x500) / RF(0x5c);
            float share = !(1.0f > ratio) ? 1.0f : ratio;
            RF(0xc0) = grip * RF(0x98) * share * RF(0xc0);
        }
    }
    float turned = RF(0x4e8) + RF(0xbc);
    float sum = 0.0f;
    PF_VAR(pf);
    sum = tyre(c, 2, turned) + sum;
    sum = tyre(c, 0, RF(0x4e8) + RF(0xbc)) + sum;
    sum = tyre(c, 3, RF(0x4e8)) + sum;
    sum = tyre(c, 1, RF(0x4e8)) + sum;
    PF_ADD(PF_TYRE, pf); PF_SET(pf);
    RF(0xc34) = 1.0f - (sum * 0.25f) / (RF(0x5c) * 111.0f);
    *squeal = 0;
    if(!(RF(0x510) <= 0.0f) && !(RF(0xc34) >= 0.93f)) *squeal = 1;
    angles_from_corners(c);
    PF_ADD(PF_ANGLES, pf); PF_SET(pf);
    corners(c);
    PF_ADD(PF_CORNERS, pf); PF_SET(pf);
    find_road(cut(RF(0x4d8)), cut(RF(0x4e0)));
    PF_ADD(PF_FIND_ROAD, pf);
    RF(0x4c4) = G.road_heading;
}

/* 0x1b92b: the car's step on the ground */
static void ground_step(car_t *c)
{
    if(RI(0x58) != 0) {
        float spin = (float)RI(0x58);
        if(ang_diff(RF(0x54), RF(0x4e8)) * spin < 0.0f) RI(0x58) = 0;
        else if(!(0.0f <= (float)RI(0x58) * RF(0x660))) RI(0x58) = 0;
        else ease_angle(&RF(0x4e8), (float)RI(0x58) * 0.1f + RF(0x54), 0x57);
    }
    PF_VAR(pf);
    corners(c);
    PF_ADD(PF_CORNERS, pf); PF_SET(pf);
    springs(c);
    PF_ADD(PF_SPRINGS, pf); PF_SET(pf);
    speed_and_way(c);
    PF_ADD(PF_SPEED, pf);
    if(RI(0x150) != 0) push_sideways(c, RF(0x160));
    if(RI(0x58) != 0) {
        float m[3];
        for(int k = 0; k < 3; k++) m[k] = (((RF(0x544 + 4 * k) + RF(0x550 + 4 * k)) + RF(0x55c + 4 * k)) + RF(0x568 + 4 * k)) * 0.25f;
        for(int w = 0; w < 4; w++) for(int k = 0; k < 3; k++) RF(0x544 + 0xc * w + 4 * k) = m[k];
    }
    if(fabsf(ang_diff(RF(0x4e8), RF(0x4c4))) >= 1.570796370506286f) {
        RF(0x1f0) = RF(0x1f0) - DT;
        if(!(RF(0x1f0) >= -1.0f)) RF(0x1f0) = -1.0f;
    } else {
        RF(0x1f0) = DT + RF(0x1f0);
        if(RF(0x1f0) > 1.0f) RF(0x1f0) = 1.0f;
    }
    PF_SET(pf);
    body_lean(c);
    PF_ADD(PF_LEAN, pf);
}

/* 0x1f697: one gear up or down, not while the last change settles (+0x140) */
static void change_gear(car_t *c, int by)
{
    int gear = RI(0xc7c) + by;
    if(gear > RI(0x68) || gear < 0 || by == 0 || RF(0x140) > 0.0f) return;
    RI(0xc7c) = gear;
    float revs = gear_ratio[gear] * 0.7f;
    revs *= RF(0x60);
    revs *= RF(0x4fc) / RF(0x5c);
    revs += RF(0xc78) * 0.3f;
    RF(0xc78) = revs;
    RF(0x140) = 0.5f;
}

/* 0x1f756: the engine every frame: the gearbox, the revs, the speed it turns the wheels at */
static void engine(car_t *c, const struct s2car_input *in, int manual)
{
    const float step = DT;
    if(RI(100) == 1) {
        RF(0x140) = RF(0x140) - DT;
        if(RF(0x140) < 0.0f) RF(0x140) = 0.0f;
    }
    if(c->hold) RI(0xc7c) = 0;                          /* on the grid: neutral until the race is let go */
    else if(manual) {
        if(in->gear_up) change_gear(c, 1);
        else if(in->gear_down) change_gear(c, -1);
        else RF(0x140) = 0.0f;
    } else {
        if(RI(0xc7c) == 0) { RF(0x140) = 0.5f; RI(0xc7c) = 1; }
        float most = RF(0x60), grip = RF(0x1ec), revs = RF(0xc78);
        if(revs > most * 0.8f - grip * 0.1f) {
            if(RF(0x510) == 1.0f && fabsf(RF(0xc34)) > 0.89f) change_gear(c, 1);
        } else if(revs < most * 0.55f + grip * 0.1f && RI(0xc7c) > 1 && RF(0x510) == 1.0f)
            change_gear(c, -1);
    }
    if(RF(0xc78) > RF(0x60) * 1.1f) RF(0xc78) = RF(0x60) * 1.1f;
    int gear = RI(0xc7c);
    float ratio = gear >= 0 ? gear_ratio[gear] : 0.0f;
    if(gear != 0 && 0.0f < RF(0x510)) {
        float w = fabsf(RF(0xc34)) * (step * 4.0f);
        if(1.0f <= w) w = 1.0f;
        float wheels = w * RF(0x500);
        wheels *= fabsf(cosf(RF(0x4d4)));
        wheels *= RF(0x60) / RF(0x5c);
        wheels *= ratio;
        RF(0xc78) = wheels + (1.0f - w) * RF(0xc78);
    }
    float r = RF(0xc78) / RF(0x60);
    float cc = gear != 0 ? 1.6f : 1.5f;
    float q = ratio * 0.22f + (r * cc + -1.0f);
    float pull = (1.0f - q * q) * (ratio * ratio) * 0.8f;
    if(0.0f > pull) pull = pull * 2.0f;
    float hurt = 0.0f;
    if(0.0f < RF(8) + -1.0f) hurt = RF(8) + -1.0f;
    float force = pull * RF(0x60) * RF(0x1ec) * RF(0x70) * (1.0f - hurt * 0.3f);
    float load = 0.0f;
    if(gear != 0) load = ratio * 0.5f * RF(0xc34) * RF(0x510) * RF(0x60) * RF(0x500) / RF(0x5c);
    load = RF(0x60) * 0.2f + load;
    force = force - load;
    RF(0xc78) = step * force + RF(0xc78);
    if(0.0f > RF(0xc78)) RF(0xc78) = 0.0f;
    else if(0.0f < RF(0x510)) {
        if(RF(0xc78) > RF(0x60) * 0.98f) RF(0xc78) = RF(0x60) * 0.98f;
    } else if(RF(0xc78) > RF(0x60)) RF(0xc78) = RF(0x60);
    if(gear != 0) {
        if(0.0f < RF(0x510)) {
            RF(0x4fc) = RF(0xc78) / ratio * (RF(0x5c) / RF(0x60));
            if(RI(0x4c) & 0x10) RF(0x4fc) = RF(0x4fc) + -2.5f;
            if(c->ai) RF(0x4fc) = RF(0x4fc) - RF(0x48);       /* the computer's pace (2da06): what the level takes off */
        }
        if(RF(0x4fc) < 10.0f && RF(0x1ec) < 0.1f) RF(0x4fc) = 0.0f;
    } else RF(0x4fc) = RF(0x500);
    if(RF(0xc78) > RF(0x60)) RF(0xc78) = RF(0x60);
    else if(0.0f > RF(0xc78)) RF(0xc78) = 0.0f;
}

/* 0x19409's end: the wheels' angle (0x660) and the throttle make the angle the front tyres are turned to (0xbc): put at once
   for a car with no controls (direct), else eased */
static void steering_out(car_t *c, int direct)
{
    float cs = fabsf(cosf(RF(0x4d4)));
    float grip = (cs * cs) * (cs * cs);
    float steer = (RF(0x660) / (RF(0x500) * 4.0f / RF(0x5c) + 2.0f)) * (1.6f - RF(0x1ec) * 0.6f) * grip;
    if(!(0.0f >= RF(0x4d4) * steer)) steer = (fabsf(sinf(RF(0x4d4))) * 1.5f + 1.0f) * steer;
    if(direct) RF(0xbc) = steer;
    else ease(&RF(0xbc), steer, cut(21.0f - RF(0x2c) * 5.0f));
    if(RI(0x4c) & 4) { RI(0x1ec) = 0; RI(0xbc) = 0; RF(0x1e8) = 1.0f; RI(0x1e4) = 0; RI(0x660) = 0; }
    RF(0x580) = RF(0x660);
}

/* 0x19409: the steering. keys (the d-pad): the whole lock to the side held (the game's control type 1);
   stick: an axis through the game's curve (type 0, and the car's most steering x 1.3) */
static void steering(car_t *c, const struct s2car_input *in, int stick)
{
    float axis;                                         /* the game's word: - left .. + right, in 1024ths */
    if(stick) {
        float s = in->steer;
        int32_t b0 = s <= -1.0f ? 1023 : s >= 1.0f ? -1023 : (int32_t)(-s * 1023.0f);       /* the joystick word, + to the left */
        axis = (float)(-b0) * 0.0009765625f;
    } else {
        int32_t b0 = in->steer < -0.3f ? 1023 : 0, b4 = in->steer > 0.3f ? 1023 : 0;
        axis = (float)b4 * 0.0009765625f + (float)(-b0) * 0.0009765625f;
    }
    if(axis == 0.0f) RF(0x660) = 0.0f;
    else if(!stick) RF(0x660) = (-axis < 0.0f ? -1.0f : 1.0f) * RF(0x6c);
    else {
        float x = axis / 1.0f;                          /* 0x112e20, the steering's full travel */
        float r;
        if(!(x > -1.0f)) r = -1.0f;
        else if(!(1.0f > x)) r = 1.0f;
        else r = x;
        if(!(fabsf(r) >= 0.6f)) RF(0x660) = RF(0x6c) * (r * 0.5f);
        else {
            float sign = r < 0.0f ? -1.0f : 1.0f;
            RF(0x660) = sign * ((fabsf(r) + -0.6f) * 1.7f + 0.3f) * RF(0x6c);
        }
        if(RF(0x660) > RF(0x6c)) RF(0x660) = RF(0x6c);
        else if(-RF(0x6c) > RF(0x660)) RF(0x660) = -RF(0x6c);
    }
    steering_out(c, 0);
}

/* 0x174ca and 0x1756f: the throttle and the brake from the controls (1023ths); the brake split front / rear */
static void pedals(car_t *c, const struct s2car_input *in)
{
    float t = in->throttle < 0.0f ? 0.0f : in->throttle > 1.0f ? 1.0f : in->throttle;
    float b = in->brake > in->handbrake ? in->brake : (float)in->handbrake;
    b = b < 0.0f ? 0.0f : b > 1.0f ? 1.0f : b;
    int32_t tv = (int32_t)(t * 1023.0f + 0.5f), bv = (int32_t)(b * 1023.0f + 0.5f);
    RF(0x1ec) = (float)tv / 1023.0f;
    if(bv != 0) RF(0x1e8) = (float)bv / 1023.0f;
    else RF(0x1e8) = 0.0f;
    RF(0x1e4) = RF(0x1e8) * RF(0x9c);
    RF(0x1e8) = RF(0x1e8) - RF(0x1e4);
}

/* 0x16878's part: the wheels' turning and the front wheels' steering as drawn, and the car object's flags (+0x20, 0x1737b):
   a pair of wheels turning more than 0.29 turns a step blurs its hubs (0x400 for the pair the drive goes to, 0x8000 for
   the other; with four-wheel drive 0x400 by the rear pair alone), plus the damage marks (+0xb8) and 0x200 while the
   brake is down (+0x1e4 + 0x1e8: the pedal, or a computer's driver, not the slowing down) */
static void wheel_pictures(car_t *c)
{
    ease_angle(&RF(0x13c), PI_F / (RF(0x6c) * 4.0f) * RF(0x660), 0x50);
    float front = !(RF(0x90) >= 0.2f) ? RF(0x500) * cosf(RF(0x4d4)) / RF(0x5c) : RF(0x4fc) / RF(0x5c);
    float back = !(RF(0x94) >= 0.2f) ? RF(0x500) * cosf(RF(0x4d4)) / RF(0x5c) : RF(0x4fc) / RF(0x5c);
    RF(0x584) = RF(0x584) - front * PI_F;
    RF(0x664) = RF(0x664) - front * PI_F;
    RF(0x5f4) = RF(0x5f4) - back * PI_F;
    RF(0x6d4) = RF(0x6d4) - back * PI_F;
    const int fast_front = fabsf(front) > 0.29f, fast_back = fabsf(back) > 0.29f;
    uint32_t look;
    if(RF(0x90) > 0.8f) look = (fast_front ? 0x400u : 0u) | (fast_back ? 0x8000u : 0u);
    else if(RF(0x94) > 0.8f) look = (fast_back ? 0x400u : 0u) | (fast_front ? 0x8000u : 0u);
    else look = fast_back ? 0x400u : 0u;
    c->look = look | (uint32_t)RI(0xb8) | (RF(0x1e8) + RF(0x1e4) != 0.0f ? 0x200u : 0u);
}

/* 0x16824: the car against the walls, then the ground under the wheels */
static void walls_ground(car_t *c)
{
    PF_VAR(pf);
    struct corner_ground under[4];
    look_under_corners(c, under);
    PF_ADD(PF_LOOK, pf); PF_SET(pf);
    against_wall(c, under);                             /* 0x29571 */
    PF_ADD(PF_WALL, pf); PF_SET(pf);
    ground_under_wheels(c, under);                      /* 0x28f96 */
    PF_ADD(PF_GROUND, pf);
}

/* 0x2baa4's passes over the cars before the touching cars' solver, for one car: the tyres (0x1b36f), the walls and the ground
   (0x16824), the step on the ground (0x1b92b) */
static void move_pass(car_t *c)
{
    PF_VAR(pf);
    tyres(c, &c->squeal);
    PF_ADD(PF_TYRES, pf); PF_SET(pf);
    walls_ground(c);
    ground_step(c);                                     /* 0x1b92b */
    PF_ADD(PF_GSTEP, pf);
}

/* 0x2baa4 for one car alone, after the solver: the wheels' picture (0x16878), the clocks and the engine (0x167e2's first part) */
static void frame_rest(car_t *c, const struct s2car_input *in, int manual)
{
    PF_VAR(pf);
    wheel_pictures(c);                                  /* 0x16878 */
    PF_ADD(PF_WPIC, pf); PF_SET(pf);
    RF(0x30) = RF(0x30) - DT;                           /* 0x1f3ce: the knock's clocks run down */
    if(RF(0x30) < 0.0f) RF(0x30) = 0.0f;
    RF(4) = RF(4) - DT;
    if(RF(4) < 0.0f) RF(4) = 0.0f;
    engine(c, in, manual);                              /* 0x167e2: the clocks, the engine, the controls */
    PF_ADD(PF_ENGINE, pf);
}

/* ------------------------------------------------------------------------------------- the camera: 0x13f38 */

#define CAM_DIST    1300.0f
#define CAM_ABOVE   200
#define CAM_PITCH   (-0.274f)

/* camst: 0 distance, 1 pitch, 2 yaw, 3 roll, 4 height (a whole number as a float) */
static void chase_camera(car_t *c)
{
    float *cs = c->camst;
    float speed = RF(0x500), top = RF(0x5c);
    float way = RF(0x508);
    if(way > 6.28318548f) way += -TWO_PI_F;
    else if(0.0f > way) way += TWO_PI_F;
    int ease_i = cut(95.0f - speed / top * 10.0f);
    float share = speed / top;
    float lead = ang_diff(RF(0x4e8), way) * 0.3f * share;
    float yaw = ang_diff(RF(0x4e8), lead);
    if(!c->cam_started) {
        c->cam_started = 1;
        cs[0] = CAM_DIST;
        cs[3] = 0.0f;
        cs[1] = CAM_PITCH;
        cs[2] = yaw;
        cs[4] = (float)cut((float)(RI(0x4dc) + CAM_ABOVE) - sinf(cs[1]) * cs[0]);
    } else {
        ease(&cs[0], CAM_DIST, 0x50);
        ease_angle(&cs[3], 0.0f, ease_i);
        ease_angle(&cs[1], CAM_PITCH, ease_i);
        ease_angle(&cs[2], yaw, ease_i);
        int height = cut((float)(RI(0x4dc) + CAM_ABOVE) - sinf(cs[1]) * cs[0]);
        int32_t hi = cut(cs[4]);
        ease_int(&hi, (float)height, 0x3c);
        cs[4] = (float)hi;
    }
}

/* ------------------------------------------------------------------------------------ outputs and set-up */

static void outputs(car_t *c, int squeal)
{
    c->pos[0] = RF(0x4d8);
    c->pos[1] = RF(0x4e0);
    c->pos[2] = (float)RI(0x4dc);
    c->yaw = RF(0x4e8);
    c->pitch = RF(0x4e4);
    c->roll = RF(0x4ec);
    c->body_pitch = RF(0xc18);
    c->body_roll = RF(0xc20);
    float sy = sinf(c->yaw), cy = cosf(c->yaw), sp = sinf(c->pitch), cp = cosf(c->pitch), sr = sinf(c->roll), cr = cosf(c->roll);
    /* forward = (sin yaw cos pitch, -cos yaw cos pitch, sin pitch), the car's side across it, up from the two */
    float f[3] = { sy * cp, -cy * cp, sp };
    float r[3] = { -cy * cr + sy * sp * sr, -sy * cr - cy * sp * sr, cp * sr };
    float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    for(int k = 0; k < 3; k++) { c->rot[3 * k] = r[k]; c->rot[3 * k + 1] = u[k]; c->rot[3 * k + 2] = f[k]; }
    float dx = RF(0x4d8) - RF(0x734), dy = (float)(RI(0x4dc) - RI(0x738)), dz = RF(0x4e0) - RF(0x73c);
    c->vel[0] = dx / DT; c->vel[1] = dz / DT; c->vel[2] = dy / DT;
    c->speed_kmh = RF(0x500) * 1.513f;                  /* the dial: the port's factor (docs/screamer2/driving.md) */
    c->speed_mph = c->speed_kmh * 0.6214f;
    c->rpm = RF(0xc78);
    c->gear = RI(0xc7c);
    c->wheel_steer = RF(0x13c);
    c->wheel_spin[0] = RF(0x584); c->wheel_spin[1] = RF(0x5f4); c->wheel_spin[2] = RF(0x664); c->wheel_spin[3] = RF(0x6d4);
    for(int i = 0; i < 4; i++) c->susp[i] = RF(WH(i) + 0x18) - RF(WH(i) + 0x14);
    c->on_ground = (int)(RF(0x510) * 4.0f + 0.5f);
    c->skid_flags = 0;
    for(int i = 0; i < 4; i++) if(RI(WH(i) + 0x60)) c->skid_flags |= 1 << i;
    if(squeal) c->skid_flags |= 16;
    c->hit_wall = RI(0x150) != 0;
    c->off_track = 0;
    if(RI(0x150)) {                                     /* some corner is off the road: and the middle? */
        int held, h, n[3], k;
        track_ground_s2(cut(RF(0x4d8)), cut(RF(0x4e0)), &held, &h, n, &k);
        c->off_track = !held;
    }
    if(c->ai && !c->cam_on) {                           /* no camera follows a computer's car (unless the replay's does: it then begins where it goes) */
        c->cam_started = 0;
        return;
    }
    chase_camera(c);
    c->cam[3] = c->camst[2];
    c->cam[4] = c->camst[1];
    c->cam[5] = c->camst[3];
    c->cam[0] = RF(0x4d8) - sinf(c->cam[3]) * c->camst[0] * cosf(c->cam[4]);
    c->cam[1] = cosf(c->cam[3]) * c->camst[0] * cosf(c->cam[4]) + RF(0x4e0);
    c->cam[2] = c->camst[4];
}

void s2car_move(struct s2car_state *s, int driven)
{
    car_t *c = s;
    RI(100) = (c->manual && !driven) ? 0 : 1;
    RI(0x4c) &= ~8;
    move_pass(c);
}

void s2car_move_again(struct s2car_state *s, float dt)
{
    car_t *c = s;
    step_dt = dt;
    move_pass(c);
    step_dt = DT;
}

void s2car_walls(struct s2car_state *s)
{
    walls_ground(s);
}

void s2car_finish(struct s2car_state *s, const struct s2car_input *in)
{
    car_t *c = s;
    frame_rest(c, in, c->manual);
    pedals(c, in);
    steering(c, in, c->stick);
    PF_VAR(pf);
    outputs(c, c->squeal);
    PF_ADD(PF_OUTPUTS, pf);
}

void s2car_step(struct s2car_state *s, const struct s2car_input *in)
{
    s2car_move(s, 0);
    s2car_finish(s, in);
}

void s2car_finish_driven(struct s2car_state *s, void (*driver)(struct s2car_state *s, void *ctx), void *ctx)
{
    static const struct s2car_input none;
    car_t *c = s;
    frame_rest(c, &none, 0);
    RF(0x1e8) = 0.0f;                                   /* 0x1756f: no pedals */
    RF(0x1e4) = 0.0f;
    PF_VAR(pf);
    driver(s, ctx);                                     /* 0x19409: the road under the car (0x153c6), the driver (0x17726) */
    PF_ADD(PF_DRIVER, pf); PF_SET(pf);
    if((RI(0x4c) & 1) && (RI(4) & 0x7fffffff) == 0) { RI(0x1ec) = 0; RF(0x1e8) = 1.0f; }     /* finished and the time is up */
    steering_out(c, c->ai);
    PF_ADD(PF_STEER, pf); PF_SET(pf);
    outputs(c, c->squeal);
    PF_ADD(PF_OUTPUTS, pf);
}

void s2car_step_driven(struct s2car_state *s, void (*driver)(struct s2car_state *s, void *ctx), void *ctx)
{
    s2car_move(s, 1);
    s2car_finish_driven(s, driver, ctx);
}

void s2car_knock(struct s2car_state *s, int side, int corner, float share, float force)
{
    knock(s, side, corner, share, force);
}

int s2car_lines_meet(const float p[3], const float d[3], const float q[3], const float e[3], float *s, float *u)
{
    return lines_meet(p, d, q, e, s, u);
}

float s2car_ground_grip(const struct s2car_state *s)
{
    const car_t *c = s;
    const int ts = RI(0x50) < 0 || RI(0x50) > 2 ? 0 : RI(0x50);
    return ((kind_of(RI(0x6b0))->grip[ts] + kind_of(RI(0x5d0))->grip[ts]) + kind_of(RI(0x720))->grip[ts] + kind_of(RI(0x640))->grip[ts]) * 0.25f;
}

const struct s2_road_kind *s2car_road_kind(int id)
{
    return kind_of(id);
}

int s2car_wall_near(int x, int z, int *wx, int *wz, float *angle)
{
    const int i = nearest(1, x, z);
    if(i < 0) return 0;
    *wx = G.walls[i].x;
    *wz = G.walls[i].y;
    *angle = G.walls[i].angle;
    return 1;
}

int s2car_road_near(int x, int z, float *heading)
{
    const int i = nearest(0, x, z);
    if(i < 0) return 0;
    float a = G.dirs[i].angle;
    if(a > TWO_PI_F) a += -TWO_PI_F;
    *heading = a;
    return 1;
}

float s2car_ang_diff(float a, float b) { return ang_diff(a, b); }
float s2car_ground_angle(float x, float z) { return ground_angle(x, z); }
void s2car_turn_up(int32_t out[3], const int32_t in[3], float angle) { turn_up(out, in, angle); }

/* the reflection's picture by the kind of the ground under the fourth wheel (record +0x6b0): s2env.h */
uint32_t s2car_reflection(const struct s2car_state *s, float *rate)
{
    const car_t *c = s;
    return s2_env_phase(RI(0x6b0), rate);
}

const uint8_t *s2car_map_dat(uint32_t *size)
{
    if(size) *size = G.map_size;
    return G.map_dat;
}

const uint8_t *s2car_game_dat(uint32_t *size)
{
    if(size) *size = G.game_size;
    return G.game_dat;
}

/* ----------------------------------------------------------------------------------------------- loading */

/* the first node of the car's line (MAPnOPP.SPL: a count, a start, 0x38-byte nodes with x at +4 and z at +8) */
static int read_start(void)
{
    char name[16] = "MAP0OPP.SPL";
    struct dat_entry e;
    name[3] = (char)('0' + G.track);
    if(!dat_find(G.map_dat, G.map_size, name, &e)) return 0;
    uint32_t n = dat_extract(G.map_dat, G.map_size, &e, G.text, sizeof G.text);
    if(n < 8 + 0x38) return 0;
    int32_t x, z;
    memcpy(&x, G.text + 8 + 4, 4);
    memcpy(&z, G.text + 8 + 8, 4);
    G.start_x = x;
    G.start_z = z;
    return 1;
}

#ifndef __riscv
static uint8_t *host_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if(!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 8);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(p); return 0; }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}
#endif

static int load_data(int track)
{
    if(G.loaded_track == track && G.cars_ok && G.game_dat && G.map_dat) return 1;      /* a reset on the same track */
#ifdef __riscv
    struct zip *z = files_zip();
    if(!z) return 0;
    if(!G.game_dat) {
        const struct zip_entry *e = zip_find(z, "DATA/GAME.DAT");
        if(!e || e->size > 0x2000 || zip_read(z, e, G.game_buf, 0x2000)) return 0;
        G.game_dat = G.game_buf;
        G.game_size = e->size;
        G.cars_ok = 0;
    }
    if(G.map_dat != G.map_buf || G.loaded_track != track) {
        char name[16] = "DATA/MAP0.DAT";
        name[8] = (char)('0' + track);
        const struct zip_entry *e = zip_find(z, name);
        if(!e || e->size > 0x6000 || zip_read(z, e, G.map_buf, 0x6000)) return 0;
        G.map_dat = G.map_buf;
        G.map_size = e->size;
    }
#else
    /* on the host (tools/s2/render_s2.sh, the tests): the unzipped game folder S2_GAME, or what s2car_set_data() handed over */
    if(!G.game_dat || !G.map_dat || G.loaded_track != track) {
        const char *dir = getenv("S2_GAME");
        if(dir && (!G.game_dat || !G.map_dat || G.loaded_track != track)) {
            char path[512];
            uint32_t gs, ms;
            snprintf(path, sizeof path, "%s/DATA/GAME.DAT", dir);
            uint8_t *g = host_file(path, &gs);
            snprintf(path, sizeof path, "%s/DATA/MAP%d.DAT", dir, track);
            uint8_t *m = host_file(path, &ms);
            if(g && m) s2car_set_data(g, gs, m, ms);
        }
    }
    if(!G.game_dat || !G.map_dat) return 0;
#endif
    G.track = track;
    if(!G.cars_ok) {
        if(!s2_params_cars_dat(G.game_dat, G.game_size, G.text, sizeof G.text, G.cars)) return 0;
        G.cars_ok = 1;
    }
    if(!s2_params_track_dat(G.map_dat, G.map_size, track, G.text, sizeof G.text, &G.trk)) return 0;
    memset(G.kind, 0, sizeof G.kind);
    for(int k = 0; k < S2_KINDS; k++)
        if(G.trk.kind[k].id >= 0 && G.trk.kind[k].id < KINDS) G.kind[G.trk.kind[k].id] = G.trk.kind[k];
    if(!read_start()) return 0;
    G.loaded_track = track;
    return 1;
}

/* the image's car model of a car (team 0 .. 3, car 0 .. 4): the cars section has a row for each model whose livery word
   names its car (team * 256 + car, from 1: track_build_s2()); a car not in the image takes a model of the same car
   number (any team's: the wheels are the car's), and an image that names none (the rows' livery is 0) has the cars
   AUTO1 .. 5 in turn */
static int image_model(int team, int car)
{
    int n, same = -1;
    const struct car_spec *rows = track_cars(&n);
    for(int k = 0; k < n; k++) {
        if(rows[k].livery == (team + 1) * 256 + car + 1) return k;
        if((rows[k].livery & 255) == car + 1 && same < 0) same = k;
    }
    return same >= 0 ? same : car;
}

/* the wheel blocks of the car's model in the image: "flx", "blx", "frx", "brx" for wheels 0 .. 3; their place in the
   car (the block's frame x, y, z) */
static int wheel_places(int team, int car, int32_t pos[4][3])
{
    static const char *const names[4] = { "flx", "blx", "frx", "brx" };
    int nm, model = image_model(team, car);
    track_models(&nm);
    if(model < 0 || model >= nm) return 0;
    const uint8_t *m = track_model_mesh(model);
    int32_t nblk;
    memcpy(&nblk, m + 0x14, 4);
    if(nblk < 1 || nblk > 64) return 0;
    int found = 0;
    for(int b = 0; b < nblk; b++) {
        int32_t off, so;
        memcpy(&off, m + 0x18 + 4 * b, 4);
        memcpy(&so, m + off + 0x0c, 4);
        if(so <= 0) continue;
        for(int w = 0; w < 4; w++)
            if(!memcmp(m + off + 0x68, names[w], 4)) {
                memcpy(&pos[w][0], m + so + 0x10, 4);
                memcpy(&pos[w][1], m + so + 0x14, 4);
                memcpy(&pos[w][2], m + so + 0x18, 4);
                found |= 1 << w;
            }
    }
    return found == 15;
}

static int clamp_to(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* 0x14e20: the car made of its set-up, the six numbers of the race's record (docs/screamer2/driving.md, "The set-up"): the arcade's 2 2 -1 2 2 2 (-1: the track's own tyres) unless
   the car is given another (`custom`: the CUSTOMIZE page's, the ghost's); the numbers are held to the page's ranges, the save may hold anything within -1 .. 4 */
static void make_car(car_t *c, int track)
{
    int set[6] = { 2, 2, -1, 2, 2, 2 };
    if(c->custom) {
        set[0] = clamp_to(c->setup.handling, 0, 4);
        set[1] = clamp_to(c->setup.pressure, 0, 4);
        set[2] = clamp_to(c->setup.tyres, -1, 2);
        set[3] = clamp_to(c->setup.front, 0, 4);
        set[4] = clamp_to(c->setup.rear, 0, 4);
        set[5] = clamp_to(c->setup.brakes, 0, 4);
    }
    RI(0x50) = set[2] < 0 ? tyre_by_track[track - 1] : set[2];
    if(RF(0x94) > 0.8f && c->ai) set[0] = 1;            /* a computer's rear-drive car gets the handling 1 */
    int tyre_set = RI(0x50);
    if(c->ai && c->tuned) {                             /* a computer's car where the mode word has no bit 1: the tyres' best pressure, springs 4 (the rear's 3 for a car that is not
                                                           four-wheel drive), softer by the track (England's neighbours 1 and 2: one, track 5: two), the most handling */
        const int softer = track - 1 >= 1 && track - 1 <= 2 ? 1 : track - 1 == 4 ? 2 : 0;
        set[1] = G.trk.tyre_pressure[tyre_set];
        set[3] = 4;
        set[4] = fabsf(RF(0x94) - RF(0x90)) > 0.4f ? 3 : 4;
        set[3] -= softer;
        set[4] -= softer;
        set[0] = 4;
    }
    float step = 0.01f;                                 /* |0x94 - 0x90| > 0.4: 0.005 */
    if(fabsf(RF(0x94) - RF(0x90)) > 0.4f) step = 0.005f;
    RF(0x70) = 1.0f;
    int off = G.trk.tyre_pressure[tyre_set] - set[1];
    RF(0x74) = (1.1f - (float)(off < 0 ? -off : off) * 0.05f) * RF(0x74);
    int front = 2 - set[3], rear = 2 - set[4];
    RF(0x78) = (float)front * step + RF(0x78);
    RF(0x80) = RF(0x78);
    RI(0x59c) = RI(0xa0) + front * 5;
    RI(0x67c) = RI(0x59c);
    RF(0x594) = (float)(RI(0xa8) + front * 5);
    RF(0x674) = RF(0x594); RF(0x58c) = RF(0x674); RF(0x66c) = RF(0x58c);
    RF(0x7c) = (float)rear * step + RF(0x7c);
    RF(0x84) = RF(0x7c);
    RI(0x60c) = RI(0xa0) + rear * 5;
    RI(0x6ec) = RI(0x60c);
    RF(0x604) = (float)(RI(0xa8) + rear * 5);
    RF(0x6e4) = RF(0x604); RF(0x5fc) = RF(0x6e4); RF(0x6dc) = RF(0x5fc);
    RF(0x5c) = RF(0x5c) * 1.12f;
    RF(0x5c) = (float)(set[1] - 2 - tyre_factor[tyre_set]) + RF(0x5c);
    RF(0x6c) = ((float)(set[0] - 2) * 0.15f + 0.7f) * RF(0x6c);
    if(c->ai) RF(0x6c) = RF(0x6c) * 1.3f;               /* a car with no controls (or a stick) steers 1.3 times as far */
    RF(0x9c) = (float)set[5] * 0.25f;
    RI(8) = 0;
    for(int i = 0; i < 4; i++) { RI(0xc + 4 * i) = 0; RI(0x1c + 4 * i) = 0; }
    RF(0x2c) = (float)(set[3] + set[4]) * 0.5f;
}

static int init_car(struct s2car_state *s, int team, int car, int track, int grid, int ai, const float *at, int abreast)
{
    car_t *c = s;
    if(team < 0 || team > 3 || car < 0 || car > 4 || track < 1 || track > 7 || grid < 0 || grid > 15) return 0;
    c->ai = ai != 0;
    c->hold = 1;
    if(!load_data(track)) return 0;
    index_track();
    int32_t wp[4][3];
    if(!wheel_places(team, car, wp)) return 0;
    G.wall = 0;
    G.road_found = 0;
    G.road_heading = 0.0f;
    memset(c->rec, 0, sizeof c->rec);
    memset(c->camst, 0, sizeof c->camst);
    c->cam_started = 0;
    RI(0x4c) = 0x80;
    memcpy((uint8_t *)c->rec + 0x5c, &G.cars[team * 5 + car], 0x58);
    RI(100) = c->manual ? 0 : 1;
    RF(0x13c) = 0.0f;
    RF(0x76c) = 1.0f;
    RF(0x510) = RF(0x76c);
    for(int i = 0; i < 4; i++) {
        int w = WH(i);
        RI(w + 0x38) = wp[i][0]; RI(w + 0x3c) = wp[i][1]; RI(w + 0x40) = wp[i][2];
        RF(w + 0x18) = (float)RI(0xa4);
        RF(w + 0x20) = (float)RI(0xa8);
    }
    RI(0x4d0) = RI(0x5b4) - RI(0x624) < 0 ? RI(0x624) - RI(0x5b4) : RI(0x5b4) - RI(0x624);
    RI(0x4c8) = RI(0x5ac) - RI(0x68c) < 0 ? RI(0x68c) - RI(0x5ac) : RI(0x5ac) - RI(0x68c);
    RF(0xc34) = 1.0f;
    float x0 = at ? at[0] : (float)G.start_x, z0 = at ? at[1] : (float)G.start_z;
    RF(0x4d8) = s2car_placed(x0);
    RF(0x4e0) = s2car_placed(z0);
    RI(0x4dc) = -100000;
    for(int i = 0; i < 4; i++) {
        int w = WH(i);
        RI(w + 0x30) = -100000;
        RI(w + 8) = 1;
    }
    find_road(cut(RF(0x4d8)), cut(RF(0x4e0)));
    RF(0x4c4) = G.road_heading;
    RF(0x4e8) = RF(0x4c4);
    RF(0x508) = RF(0x4e8);
    /* the grid (0x2f519): place g is 1000 units to the side (the sides in turn) and 2000 x g back of the line's first node; in time attack and the
       free run (abreast) the cars stand side by side, no row behind (0x30328 .. 0x303fa: only the 1000 to the side) */
    int k = (grid % 2) * 2 - 1;
    float a = RF(0x4e8);
    const float back = abreast ? 0.0f : (float)grid;
    float gx = (float)k * sinf(a + HALF_PI_F) * 1000.0f + back * sinf(a + PI_F) * 2000.0f + x0;
    float gz = z0 - ((float)k * cosf(a + HALF_PI_F) * 1000.0f + back * cosf(a + PI_F) * 2000.0f);
    if(at) { gx = x0; gz = z0; }                        /* put back on the line itself (a car that left the road) */
    RF(0x4d8) = s2car_placed(gx);
    RF(0x4e0) = s2car_placed(gz);
    memmove((uint8_t *)c->rec + 0x734, (uint8_t *)c->rec + 0x4d8, 0x25c);
    corners(c);
    struct corner_ground under[4];
    look_under_corners(c, under);
    against_wall(c, under);
    ground_under_wheels(c, under);
    for(int i = 0; i < 4; i++) {
        int w = WH(i);
        RI(w) = 0;
        RI(w + 0x30) = cut((float)RI(w + 0x44) + RF(w + 0x18));
        RF(0x518 + 0xc * i) = (float)RI(w + 0x30);
    }
    springs(c);
    for(int i = 0; i < 4; i++) RI(WH(i)) = 0;
    memmove((uint8_t *)c->rec + 0x734, (uint8_t *)c->rec + 0x4d8, 0x25c);
    speed_and_way(c);
    look_under_corners(c, under);
    against_wall(c, under);
    ground_under_wheels(c, under);
    make_car(c, track);
    outputs(c, 0);
    return 1;
}

int s2car_init_grid(struct s2car_state *s, int team, int car, int track, int grid, int ai)
{
    return init_car(s, team, car, track, grid, ai, 0, 0);
}

int s2car_init_row(struct s2car_state *s, int team, int car, int track, int side, int ai)
{
    return init_car(s, team, car, track, side, ai, 0, 1);
}

int s2car_init_at(struct s2car_state *s, int team, int car, int track, float x, float z, int ai)
{
    const float at[2] = { x, z };
    return init_car(s, team, car, track, 0, ai, at, 0);
}

/* the lone car of the driving mode: the first place of the grid, driven by the player, not held */
int s2car_init(struct s2car_state *s, int team, int car, int track, const struct track_image *img)
{
    (void)img;
    if(!s2car_init_grid(s, team, car, track, 0, 0)) return 0;
    s->hold = 0;
    return 1;
}

void s2car_put(struct s2car_state *s, int x, int y, float heading)
{
    car_t *c = s;
    RF(0x4d8) = (float)x; RF(0x4e0) = (float)y; RI(0x4dc) = 0;
    RF(0x4e8) = heading;
    corners(c);
    memmove((uint8_t *)c->rec + 0x734, (uint8_t *)c->rec + 0x4d8, 0x25c);
}

void s2car_camera(const struct s2car_state *s, struct track_cam *cam)
{
    *cam = (struct track_cam){ s->cam[0], s->cam[1], s->cam[2], s->cam[3], s->cam[4], s->cam[5] };
}

void s2car_look(const struct s2car_state *s, struct car_look *lk)
{
    /* car_look (car.h): the pose as the renderer takes it: yaw grows to the left (the game's too), pitch + tail up (the
       game's is nose up: negated), roll as the game's (S1's car_player_look negates S1's own roll, whose sign is the
       other way), the body's lean (the game's values are the angles the four body blocks are turned by, 0x16878) in the
       same senses as the car's angles, the wheels' turn as the game's, the steering + right (the game's: + left).
       The tilt's signs were not seen drawn on a slope. The wheels' turn is the game's sum, which grows without end (2 rad a step at speed); for the drawing it is taken
       into one turn, as Screamer 1's car.c does with its own: above 201 rad the library's sine and cosine go the long way for large arguments (__kernel_rem_pio2f),
       eight times a frame after the first seconds of a race. */
    *lk = (struct car_look){ s->pos[0], s->pos[1], s->pos[2], s->yaw, -s->pitch, s->roll, -s->body_pitch,
                             s->body_roll, fmodf(s->wheel_spin[0], 6.2831853f), -s->wheel_steer, s->look >> 2 };
}

void s2car_fx(const struct s2car_state *s, struct s2car_fx *out)
{
    const car_t *c = s;
    out->on_ground = RF(0x510) > 0.0f;
    out->slip = RF(0xc34);
    out->speed = RF(0x500);
    out->x = (int32_t)RF(0x4d8);
    out->y = RI(0x4dc);
    out->z = (int32_t)RF(0x4e0);
    out->yaw = RF(0x4e8);
    out->pitch = RF(0x4e4);
    for(int i = 0; i < 2; i++) {
        const struct s2_road_kind *k = kind_of(RI(0x5d0 + 0x70 * 2 * i));       /* (the kind under wheel 0, or 2) */
        const int w = 1 + 2 * i;                                                /* (the rear wheel beside it: 1, or 3) */
        out->rear[i].flags = k->smoke[0];
        out->rear[i].dispersion = k->smoke[1];
        out->rear[i].world[0] = RI(0x5a0 + 0x70 * w);
        out->rear[i].world[1] = RI(0x5a4 + 0x70 * w);
        out->rear[i].world[2] = RI(0x5a8 + 0x70 * w);
        for(int j = 0; j < 3; j++) out->rear[i].local[j] = RI(WH(w) + 0x38 + 4 * j);
    }
    out->ground = RI(0x4f8);
    out->slope_along = RF(0xc0c);
    out->slope_across = RF(0xc14);
}
