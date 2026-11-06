/*
 * The obstacles and the sparks (firmware/s2props.c) on the PC, run by tools/s2/check_props_s2.sh: the firmware's own code (the physics, the race, the track image of
 * tools/s2/make_track_s2.py), the SDRAM mapped at its addresses, the game's files for the car's numbers and for the obstacles (S2_GAME).
 *
 *   check_props_s2 model <image> <track> <steps>
 *       the arcade's race on the model, the game's driver driving all four cars (as in the recording of the running original, tools/s2/dosbox_props_s2.py), with the
 *       obstacles and the sparks run from the race (race_step_hook, s2car_sparks), and again without them: every car's whole record after every step is the same but
 *       for the word +0xc88 (the obstacles a car has knocked), also with the steps of a frame taken two at a time. Prints where the obstacles stand and what happened.
 *   check_props_s2 rec <image> <track> <recording> <frame bytes>
 *       the recording of the running original: the cars' records go through the firmware's code frame by frame and the obstacles' and the sparks' records of the game
 *       are compared with the firmware's (see the end of this file)
 *
 * Exit status 1 if any check fails.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "hw.h"
#include "track.h"
#include "road.h"
#include "s2car.h"
#include "race.c"                                       /* (its static state is set below, as tools/s2/race_replay_s2.c does) */
#include "s2props.h"
#include "blit.h"
#include "raster.h"
#include "s2env.h"
#include "dat.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))
#define REC         0xCC0
#define CARS        4

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 40) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void map(void)
{
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE)
        || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)(uintptr_t)SDRAM_UBASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        exit(1);
    }
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*n + 4);
    if (fread(p, 1, *n, f) != *n) { perror(path); exit(2); }
    fclose(f);
    return p;
}

static int32_t i32(const uint8_t *p, uint32_t o) { int32_t v; memcpy(&v, p + o, 4); return v; }
static float f32(const uint8_t *p, uint32_t o) { float v; memcpy(&v, p + o, 4); return v; }

/* a material record's routine, 208 and 224 as one: the see-through step (firmware/smg.h) makes 224 of both */
static uint32_t routine_of(const uint8_t *rec)
{
    uint32_t r;
    memcpy(&r, rec, 4);
    return r == 208 ? 224 : r;
}

/* what the builder reports as track_build_info.misc_delta, found in the image (tools/s2/render_s2.c has the same) */
static int32_t misc_delta(const uint8_t *gfx, uint32_t gfx_size)
{
    static uint8_t misc[0x40000];
    struct dat_entry e;
    if (!dat_find(gfx, gfx_size, "MISC.MLB", &e) || dat_extract(gfx, gfx_size, &e, misc, sizeof misc) != e.size) return 0;
    const uint8_t *img = TEX_FILE;
    uint32_t mlb_off = (uint32_t)(track_mlb() - img), nmisc = s2_mlb_records(misc, e.size), total = (mlb_off - 0x100) / 16;
    for (uint32_t i = total - nmisc + 1; i-- > 0;) {
        uint32_t k = 0;
        while (k < nmisc && routine_of(img + 0x100 + 16 * (i + k)) == routine_of(misc + 16 * k) && !memcmp(img + 0x100 + 16 * (i + k) + 8, misc + 16 * k + 8, 8)) k++;
        if (k == nmisc) return (int32_t)(0x100 + 16 * i - mlb_off);
    }
    return 0;
}

/* the track's props from the game's containers, as the viewer reads them */
static int load_props(int track)
{
    char name[64];
    size_t gn, dn;
    snprintf(name, sizeof name, "%s/GFX/MAP%dH01.DAT", getenv("S2_GAME"), track);
    uint8_t *gfx = slurp(name, &gn);
    snprintf(name, sizeof name, "%s/DATA/MAP%d.DAT", getenv("S2_GAME"), track);
    uint8_t *data = slurp(name, &dn);
    static uint8_t scratch[65536] __attribute__((aligned(4)));
    const int32_t md = misc_delta(gfx, (uint32_t)gn);
    CHECK(md < 0, "track %d: MISC.MLB's records are in the image (delta %d)", track, md);
    return s2props_load(track, gfx, (uint32_t)gn, data, (uint32_t)dn, md, scratch, sizeof scratch);
}

static int open_image(const char *path)
{
    size_t n;
    uint8_t *img = slurp(path, &n);
    memcpy(TEX_FILE, img, n);
    free(img);
    return track_open(TEX_FILE) && track_init() && road_build() > 0;
}

/* ------------------------------------------------------------------------------------------------ model */

static uint32_t hash_car(const struct s2car_state *c)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof c->rec / 4; i++) {
        h ^= i == 0xc88 / 4 ? 0 : c->rec[i];            /* (the count of the obstacles a car knocked is the one word the props write) */
        h *= 16777619u;
    }
    return h;
}

#define MAXSTEPS 30000
static uint32_t world[2][MAXSTEPS];

static void race_setup(int track)
{
    race_kind = RACE_NORMAL;
    race_ghost = 0;
    race_replay = 0;
    race_laps = 3;
    race_level = 1;
    race_grid[0] = (struct s2_pick){ 1, 1 };
    race_grid[1] = (struct s2_pick){ 2, 2 };
    race_grid[2] = (struct s2_pick){ 3, 3 };
    race_grid[3] = (struct s2_pick){ 4, 4 };
    race_step_hook = 0;
    s2car_sparks = 0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    struct s2car_state *p = race_car(0);                 /* the game's driver drives the player's car too (it is marked finished with no end to its time) */
    float forever = 1e9f;
    p->rec[0x4c / 4] |= 1;
    memcpy(&p->rec[1], &forever, 4);
}

static int model(int track, int steps)
{
    CHECK(steps <= MAXSTEPS, "steps");
    int n = load_props(track);
    printf("track %d: %d obstacles\n", track, n);
    for (int run = 0; run < 2; run++) {                  /* 0: without the props, 1: with */
        race_setup(track);
        if (run) {
            s2props_begin();
            int no_ground = 0;
            for (int i = 0; i < s2props_obstacles(); i++) {
                struct s2props_rec r;
                s2props_get(0, i, &r);
                int held, h, nn[3], kind;
                float heading;
                if (!track_ground_s2((int)r.p[0], (int)r.p[2], &held, &h, nn, &kind)) no_ground++;
                CHECK(s2car_road_near((int)r.p[0], (int)r.p[2], &heading), "track %d: no road point near obstacle %d", track, i);
                if (i < 3 || getenv("ALL")) printf("  obstacle %2d at %6.0f %6.0f %6.0f angles %.3f %.3f %.3f\n", i, r.p[0], r.p[1], r.p[2], r.a[0], r.a[1], r.a[2]);
            }
            CHECK(!no_ground, "track %d: %d of %d obstacles have no ground under them", track, no_ground, s2props_obstacles());
        }
        int knocked = 0, sparks_seen = 0, was_on[S2PROPS_SPARKS] = { 0 }, first_knock = -1, flying_max = 0;
        for (int k = 0; k < steps; k++) {
            struct s2car_input in = { 0 };
            race_step(&in);
            uint32_t h = 2166136261u;
            for (int i = 0; i < RACE_CARS; i++) {
                h ^= hash_car(race_car(i));
                h *= 16777619u;
            }
            world[run][k] = h;
            if (!run) continue;
            int fl = 0, hit = 0;
            for (int i = 0; i < s2props_obstacles(); i++) {
                struct s2props_rec r;
                s2props_get(0, i, &r);
                fl += r.on;
                hit += r.hit;
            }
            if (hit > knocked) {
                if (first_knock < 0) first_knock = k;
                knocked = hit;
            }
            if (fl > flying_max) flying_max = fl;
            for (int i = 0; i < S2PROPS_SPARKS; i++) {
                struct s2props_rec r;
                s2props_get(1, i, &r);
                if (r.on && !was_on[i]) sparks_seen++;
                was_on[i] = r.on;
            }
        }
        if (run)
            printf("track %d: %d steps (%.0f s): %d obstacles knocked (first at step %d, %.1f s; %d flying at once at the most), %d sparks thrown; the cars' counts %d %d %d %d\n",
                   track, steps, steps * 0.02, knocked, first_knock, first_knock * 0.02, flying_max, sparks_seen,
                   (int)race_car(0)->rec[0xc88 / 4], (int)race_car(1)->rec[0xc88 / 4], (int)race_car(2)->rec[0xc88 / 4], (int)race_car(3)->rec[0xc88 / 4]);
    }
    int same = 1;
    for (int k = 0; k < steps && same; k++)
        if (world[0][k] != world[1][k]) { same = 0; CHECK(0, "track %d step %d: the race differs with the props on", track, k); }
    printf("track %d: the race with the props on %s the race without in every car's whole record (but +0xc88) at all %d steps\n", track, same ? "equals" : "is NOT", steps);
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the recording of the running original */

/* the frame of tools/s2/dosbox_props_s2.py: the clock, the counter T, the step, the raw time, the steps a touch cut short, the number of obstacles; the four cars' records; the
   obstacles' records (a hundred of 0x3c bytes) and the sparks' (sixty); the camera record; the object records (0x40 bytes) of the hundred obstacles' and the sixty sparks' */
#define HEAD    24
#define NPROPS  100
#define PROP    0x3C
#define CAMSZ   0xAEC
#define OBJREC  0x40
static struct { int n; const uint8_t *d; size_t frame; } rec;
static const uint8_t *frame_at(int i) { return rec.d + rec.frame * (size_t)i; }
static const uint8_t *cars_at(const uint8_t *fr) { return fr + HEAD; }
static const uint8_t *props_at(const uint8_t *fr, int spark, int i) { return fr + HEAD + CARS * REC + PROP * (size_t)(spark ? NPROPS + i : i); }
static const uint8_t *objrec_at(const uint8_t *fr, int spark, int i) { return fr + HEAD + CARS * REC + PROP * (NPROPS + 60) + CAMSZ + OBJREC * (size_t)(spark ? NPROPS + i : i); }

/* a car's record as the firmware's s2car_state: the game's words, but for the place (+0x4d8, +0x4e0 and the last step's +0x734, +0x73c), which the game keeps as whole numbers
   and the firmware as floats */
static void state_of(struct s2car_state *s, const uint8_t *r)
{
    memset(s, 0, sizeof *s);
    memcpy(s->rec, r, REC);
    static const int placed[4] = { 0x4d8, 0x4e0, 0x734, 0x73c };
    for (int k = 0; k < 4; k++) {
        const float v = (float)i32(r, (uint32_t)placed[k]);
        memcpy((uint8_t *)s->rec + placed[k], &v, 4);
    }
    s->pos[0] = (float)i32(r, 0x4d8);                   /* (the outputs the drawing reads, made from the record as s2car.c's steps make them) */
    s->pos[1] = (float)i32(r, 0x4e0);
    s->pos[2] = (float)i32(r, 0x4dc);
    s->pitch = f32(r, 0x4e4);
    s->yaw = f32(r, 0x4e8);
    s->roll = f32(r, 0x4ec);
    s->body_pitch = f32(r, 0xC18);
    s->body_roll = f32(r, 0xC20);
    s->wheel_steer = f32(r, 0x13C);
    s->wheel_spin[0] = f32(r, 0x584);
    s->look = (uint32_t)i32(r, 0xB8) | (f32(r, 0x1E8) + f32(r, 0x1E4) != 0.0f ? 0x200u : 0u);
}

static void rec_of(struct s2props_rec *o, const uint8_t *q)
{
    for (int k = 0; k < 3; k++) {
        o->p[k] = f32(q, 4 * (uint32_t)k);
        o->a[k] = f32(q, 0xc + 4 * (uint32_t)k);
        o->v[k] = f32(q, 0x18 + 4 * (uint32_t)k);
    }
    o->life = f32(q, 0x24);
    o->on = i32(q, 0x34) != 0;
    o->hit = i32(q, 0x38) != 0;
}

/* the game's rand (0xb3dd9), seeded with s */
static int game_rand(uint32_t *s)
{
    *s = *s * 0x41c64e6du + 0x3039u;
    return (int)(*s >> 16 & 0x7fff);
}

/* an angle as the game's object record keeps it (2,048ths of a turn, cut towards zero) */
static int turns2048(float a) { return (int32_t)(a * 1024.0f / 3.14159274f) & 0x7ff; }

static int near_f(float a, float b, float tol) { return fabsf(a - b) <= tol; }
/* two angles in 2,048ths of a turn within one: the firmware's float32 sum and the x87's extended one may part at a whole number */
static int near_turn(int a, int b) { return ((a - b) & 0x7ff) <= 1 || ((b - a) & 0x7ff) <= 1; }

/* the sparks the firmware's race throws when it is run a step from the state before a frame in which the game threw a batch */
static struct spark_call { float at[3], dir[3], hit; int set; } calls[64];
static int ncalls;
static void hook_record(const float at[3], const float dir[3], float hit, int set)
{
    if (ncalls < 64) calls[ncalls++] = (struct spark_call){ { at[0], at[1], at[2] }, { dir[0], dir[1], dir[2] }, hit, set };
}
struct spark_event { int frame, set, n; float at[3], dir0, dir2; };

/* the obstacles' and sparks' records of the game's frames against the firmware's code: the obstacles are the firmware's own run (stood up, the cars' records of each frame hit
   and fly them) and are compared with the game's after each frame; the sparks the game throws are taken from its records (the spawn is held against the rule: the chance, the
   place, the number) and fly on in the firmware's code, which is compared with the game's after each frame */
static int record(int track, const char *path, int frame_bytes)
{
    size_t rn;
    rec.d = slurp(path, &rn);
    rec.frame = (size_t)frame_bytes;
    rec.n = (int)(rn / rec.frame);
    const int shift = getenv("SHIFT") ? atoi(getenv("SHIFT")) : 0;
    /* the obstacles the recorder put on the cars' ways (a line each: the race clock, the car, the obstacle, where): a standing obstacle that moves in such a frame is the harness's doing */
    struct { float clock; int obstacle, seen; } pokes[512];
    int npokes = 0, pokes_seen = 0;
    {
        char pp[700], line[512];
        snprintf(pp, sizeof pp, "%.*s.pokes.txt", (int)strlen(path) - 4, path);
        FILE *pf = fopen(pp, "r");
        while (pf && fgets(line, sizeof line, pf) && npokes < 512) {
            float c;
            int car, k;
            if (sscanf(line, "%f car %d obstacle %d", &c, &car, &k) == 3) pokes[npokes++] = (typeof(pokes[0])){ c, k, 0 };
        }
        if (pf) fclose(pf);
    }
    const int nobst = load_props(track);
    printf("track %d: %d frames of the original, %d obstacles in the firmware, %d in the game\n", track, rec.n, nobst, i32(frame_at(0), 20));
    CHECK(nobst == i32(frame_at(0), 20), "the obstacles there are: %d, the game's %d", nobst, i32(frame_at(0), 20));
    race_step_hook = 0;
    s2car_sparks = 0;
    s2props_begin();
    race_step_hook = 0;
    s2car_sparks = 0;
    static struct s2car_state cars[CARS];

    /* standing: the first frame's records (the race has just begun: nothing is knocked yet) */
    int stand_ok = 0, stand_all = 0, stand_exact = 0, obj_ok = 0, obj_all = 0, obj_exact = 0;
    for (int i = 0; i < nobst; i++) {
        struct s2props_rec g, m;
        rec_of(&g, props_at(frame_at(0), 0, i));
        s2props_get(0, i, &m);
        stand_all++;
        const int exact = m.p[0] == g.p[0] && m.p[1] == g.p[1] && m.p[2] == g.p[2];
        const int ok = m.p[0] == g.p[0] && fabsf(m.p[1] - g.p[1]) <= 1.0f && m.p[2] == g.p[2] && near_f(m.a[1], g.a[1], 1e-4f) && near_f(m.a[0], g.a[0], 2e-3f) && near_f(m.a[2], g.a[2], 2e-3f) && !g.hit && !g.on;
        stand_ok += ok;
        stand_exact += exact && ok;
        if (!ok) { CHECK(0, "obstacle %d stands at %.0f %.0f %.0f angles %.4f %.4f %.4f, the game's %.0f %.0f %.0f angles %.4f %.4f %.4f", i, m.p[0], m.p[1], m.p[2], m.a[0], m.a[1], m.a[2], g.p[0], g.p[1], g.p[2], g.a[0], g.a[1], g.a[2]); }
    }
    printf("standing: %d of %d obstacles at the game's place (x and z exactly, the height within a unit: %d of them exactly) and turned as the game's (within 0.002 rad)\n", stand_ok, stand_all, stand_exact);

    int resync_cones = 0, resync_sparks = 0, gaps = 0, cone_states = 0, cone_ok = 0, hits_game = 0, hits_ok = 0, spark_states = 0, spark_ok = 0, flying_frames = 0;
    int spawns = 0, spawn_ok = 0, spawn_rand = 0, spawn_dir = 0, spawn_n = 0, spawn_n_ok = 0, spawn_near = 0, spawn_batches = 0;
    int was_on[2][NPROPS] = { { 0 } };
    float max_pos = 0.0f, max_vel = 0.0f, max_spark_pos = 0.0f;
    float last_clock = -1.0f;
    int hit_prev[NPROPS] = { 0 };
    static struct spark_event events[256];
    int nev = 0;
    for (int fi = 0; fi < rec.n; fi++) {
        const uint8_t *fr = frame_at(fi), *cf = frame_at(fi + shift < rec.n ? fi + shift : rec.n - 1);
        const float clock = f32(fr, 0);
        const uint32_t T = (uint32_t)i32(fr, 4);
        const float step = f32(fr, 8), raw = f32(fr, 12);
        const int gap = last_clock >= 0.0f && fabsf(clock - last_clock - 0.02f) > 0.004f;
        last_clock = clock;
        gaps += gap;
        for (int i = 0; i < CARS; i++) state_of(&cars[i], cars_at(cf) + REC * i);
        s2props_time(step, raw);
        s2props_step(cars, CARS, 0);
        int frame_flying = 0;
        for (int i = 0; i < nobst; i++) {                   /* the obstacles */
            struct s2props_rec g, m;
            rec_of(&g, props_at(fr, 0, i));
            s2props_get(0, i, &m);
            frame_flying += g.on;
            const int same_state = m.on == g.on && m.hit == g.hit;
            if (g.hit && !hit_prev[i]) hits_game++;
            if (g.hit && !hit_prev[i]) hits_ok += same_state;
            hit_prev[i] = g.hit;
            if (gap) { s2props_set(0, i, &g); resync_cones++; continue; }
            const float dp = fmaxf(fabsf(m.p[0] - g.p[0]), fmaxf(fabsf(m.p[1] - g.p[1]), fabsf(m.p[2] - g.p[2])));
            if (!g.on && !g.hit && dp > 1.5f) {                  /* standing, and not where the firmware has it: put there by the recorder in this frame or the one before? */
                int poked = 0;
                for (int q = 0; q < npokes && !poked; q++)
                    if (pokes[q].obstacle == i && clock >= pokes[q].clock - 0.03f && clock <= pokes[q].clock + 0.15f) { poked = 1; pokes[q].seen = 1; }
                if (poked) { s2props_set(0, i, &g); pokes_seen++; continue; }
            }
            cone_states++;
            const float dv = fmaxf(fabsf(m.v[0] - g.v[0]), fmaxf(fabsf(m.v[1] - g.v[1]), fabsf(m.v[2] - g.v[2])));
            const float da = fmaxf(fabsf(m.a[0] - g.a[0]), fmaxf(fabsf(m.a[1] - g.a[1]), fabsf(m.a[2] - g.a[2])));
            const int ok = same_state && dp <= 1.5f && da <= 0.02f && dv <= 5.0f;
            if (ok) { cone_ok++; if (dp > max_pos) max_pos = dp; if (dv > max_vel) max_vel = dv; }
            else {
                CHECK(0, "frame %d (clock %.2f) obstacle %d: firmware on %d hit %d at %.0f %.0f %.0f speed %.0f %.0f %.0f, the game's on %d hit %d at %.0f %.0f %.0f speed %.0f %.0f %.0f", fi, clock, i,
                      m.on, m.hit, m.p[0], m.p[1], m.p[2], m.v[0], m.v[1], m.v[2], g.on, g.hit, g.p[0], g.p[1], g.p[2], g.v[0], g.v[1], g.v[2]);
                s2props_set(0, i, &g);
                resync_cones++;
            }
            /* the game's object record: the place cut to whole numbers and the angles in 2,048ths of a turn, from the game's own floats */
            if (fi > 0 && (g.on || fi == 1 || fi % 50 == 0)) {         /* (the game places the objects in its first frame of the race: the record of frame 0 is empty) */
                const uint8_t *o = objrec_at(fr, 0, i);
                obj_all++;
                const int exact = i32(o, 8) == (int32_t)g.p[0] && i32(o, 0xc) == (int32_t)g.p[1] && i32(o, 0x10) == (int32_t)g.p[2] && i32(o, 0x14) == turns2048(g.a[0]) && i32(o, 0x18) == turns2048(g.a[1]) &&
                                  i32(o, 0x1c) == turns2048(g.a[2]);
                const int ok = exact || (i32(o, 8) == (int32_t)g.p[0] && i32(o, 0xc) == (int32_t)g.p[1] && i32(o, 0x10) == (int32_t)g.p[2] && near_turn(i32(o, 0x14), turns2048(g.a[0])) &&
                                         near_turn(i32(o, 0x18), turns2048(g.a[1])) && near_turn(i32(o, 0x1c), turns2048(g.a[2])));
                obj_exact += exact;
                obj_ok += ok;
                if (!ok && obj_all - obj_ok < 6) printf("  object record of obstacle %d, frame %d: place %d %d %d angles %d %d %d; from the game's floats %d %d %d, angles %d %d %d (%.5f %.5f %.5f)\n", i, fi, i32(o, 8), i32(o, 0xc), i32(o, 0x10),
                                                        i32(o, 0x14), i32(o, 0x18), i32(o, 0x1c), (int)g.p[0], (int)g.p[1], (int)g.p[2], turns2048(g.a[0]), turns2048(g.a[1]), turns2048(g.a[2]), g.a[0], g.a[1], g.a[2]);
            }
        }
        flying_frames += frame_flying > 0;
        for (int i = 0; i < 60; i++) {                      /* the sparks */
            struct s2props_rec g, m;
            rec_of(&g, props_at(fr, 1, i));
            s2props_get(1, i, &m);
            const int fresh = g.on && (!was_on[1][i] || g.life > m.life + 0.001f || !m.on);       /* thrown in this frame (or thrown again over one still flying) */
            was_on[1][i] = g.on;
            if (fresh) {
                spawns++;
                s2props_set(1, i, &g);
                resync_sparks++;
                continue;
            }
            if (!g.on && !m.on) continue;
            spark_states++;
            const float dp = fmaxf(fabsf(m.p[0] - g.p[0]), fmaxf(fabsf(m.p[1] - g.p[1]), fabsf(m.p[2] - g.p[2])));
            const int ok = m.on == g.on && dp <= 1.0f && near_f(m.life, g.life, 0.002f) && near_f(m.a[0], g.a[0], 0.01f) && near_f(m.v[1], g.v[1], 2.0f);
            spark_ok += ok;
            if (ok && dp > max_spark_pos) max_spark_pos = dp;
            if (!ok) {
                CHECK(0, "frame %d (clock %.2f) spark %d: firmware on %d life %.3f at %.0f %.0f %.0f, the game's on %d life %.3f at %.0f %.0f %.0f", fi, clock, i, m.on, m.life, m.p[0], m.p[1], m.p[2],
                      g.on, g.life, g.p[0], g.p[1], g.p[2]);
                s2props_set(1, i, &g);
                resync_sparks++;
            }
        }
        /* a batch of sparks thrown in this frame: the chance (the generator seeded with the counter of the frame or one of the four before it) makes the up speed and the three
           angles of each, the horizontal speeds are the chance and 0.9 of the direction, the same direction for the whole batch; the number is half the knock's speed */
        for (int set = 0; set < 2; set++) {
            int batch = 0, first = -1;
            for (int j = 0; j < 30; j++) {
                const int i = set ? j : 30 + j;             /* set 1 (the cars') is records 0 .. 29, set 0 (the walls') 30 .. 59 */
                struct s2props_rec g;
                rec_of(&g, props_at(fr, 1, i));
                const int fresh = g.on && fabsf(g.life - (2.0f - raw)) < 0.0015f;
                if (!fresh) break;
                batch++;
                if (first < 0) first = i;
            }
            if (!batch) continue;
            spawn_batches++;
            int found = 0;
            float dir0 = 0, dir2 = 0, dirspan = 0;
            for (int t = (int)T - 4; t <= (int)T && !found; t++) {
                uint32_t s = (uint32_t)t;
                int ok = 1;
                float lo0 = 1e30f, hi0 = -1e30f, lo2 = 1e30f, hi2 = -1e30f;
                for (int j = 0; j < batch && ok; j++) {
                    struct s2props_rec g;
                    rec_of(&g, props_at(fr, 1, first + j));
                    const int r1 = game_rand(&s) % 3000, r2 = game_rand(&s) % 3000, r3 = game_rand(&s) % 2000 + 1000, r4 = game_rand(&s) % 500, r5 = game_rand(&s) % 500, r6 = game_rand(&s) % 500;
                    const float v1 = g.v[1] + step * 7848.0f;       /* one step of the frame's flight undone */
                    const float a0 = g.a[0] + g.v[0] * step * 0.0008f, a2 = g.a[2] + g.v[2] * step * 0.00015f, a1 = g.a[1] + g.v[1] * step * 0.0004f;
                    ok = near_f(v1, (float)r3, 0.5f) && near_f(a0, (float)r4 * 0.012566371f, 5e-4f) && near_f(a2, (float)r5 * 0.012566371f, 5e-4f) && near_f(a1, (float)r6 * 0.012566371f, 5e-4f);
                    const float d0 = (g.v[0] - (float)r1 + 1500.0f) / 0.9f, d2 = (g.v[2] - (float)r2 + 1500.0f) / 0.9f;
                    if (d0 < lo0) lo0 = d0;
                    if (d0 > hi0) hi0 = d0;
                    if (d2 < lo2) lo2 = d2;
                    if (d2 > hi2) hi2 = d2;
                }
                if (ok) { found = 1; dir0 = lo0; dir2 = lo2; dirspan = fmaxf(hi0 - lo0, hi2 - lo2); }
            }
            spawn_rand += found;
            spawn_dir += found && dirspan < 3.0f;
            if (!found) CHECK(0, "frame %d: %d sparks of set %d thrown, no counter value T-4 .. T = %u gives their chance", fi, batch, set, T);
            else if (dirspan >= 3.0f) CHECK(0, "frame %d: the batch of set %d does not share one direction (spread %.1f)", fi, set, dirspan);
            if (set == 0) { /* walls: the direction's x is nought, so the horizontal x speed is the chance alone */
                struct s2props_rec g;
                rec_of(&g, props_at(fr, 1, first));
                CHECK(fabsf(dir0) < 3.0f, "frame %d: the wall's sparks have a direction x of %.1f (the game leaves it 0)", fi, dir0);
            }
            /* the number: half the speed of the car at the wall (in the units of its +0x500), one of the four cars' */
            if (set == 0) {
                spawn_n++;
                int hitn = 0;
                for (int c = 0; c < CARS; c++) {
                    const int n = (int)fminf(f32(cars_at(fr) + REC * c, 0x500) * 0.5f, 30.0f);
                    if (abs(n - batch) <= 1) hitn = 1;
                }
                spawn_n_ok += hitn;
                if (!hitn) CHECK(0, "frame %d: %d sparks of the walls' thirty, and no car's speed gives about that many", fi, batch);
            }
            /* the place: 100 above a corner of one of the cars at the time (the sparks have flown one step) */
            struct s2props_rec g;
            rec_of(&g, props_at(fr, 1, first));
            const float sx = g.p[0] - g.v[0] * step, sz = g.p[2] - g.v[2] * step;
            float best = 1e30f;
            for (int c = 0; c < CARS; c++)
                for (int k = 0; k < 4; k++) {
                    const float dx = f32(cars_at(fr) + REC * c, 0x514 + 0xc * (uint32_t)k) - sx, dz = f32(cars_at(fr) + REC * c, 0x51c + 0xc * (uint32_t)k) - sz;
                    best = fminf(best, sqrtf(dx * dx + dz * dz));
                }
            spawn_near += best < 400.0f;
            if (nev < 256 && found) events[nev++] = (struct spark_event){ fi, set, batch, { sx, g.p[1] - g.v[1] * step - 100.0f, sz }, dir0, dir2 };
        }
    }
    /* the touches and wall knocks of the game's frames run again by the firmware's race from the state of the frame before: the sparks it throws (the arguments of its call of 0x2aeb9,
       the game's step is 22 ms where the firmware's is 20: the places part a little) against the batch the game threw */
    int ev_n = 0, ev_n_ok = 0, ev_n_near = 0, ev_at = 0, ev_dir = 0, ev_late = 0, ev_matched = 0;
    for (int e = 0; e < nev; e++) {
        const int F = events[e].frame;
        if (F < 1) continue;
        race_kind = RACE_NORMAL;
        race_ghost = 0;
        race_replay = 0;
        race_level = 1;
        race_step_hook = 0;
        s2car_sparks = 0;
        if (!race_init(track, road_floor() - 3000.0f)) { CHECK(0, "race_init"); break; }
        for (int i = 0; i < RACE_CARS; i++) {
            struct s2car_state *c = &R.car[i];
            memcpy(c->rec, cars_at(frame_at(F - 1)) + REC * i, REC);
            static const int whole[4] = { 0x4d8, 0x4e0, 0x734, 0x73c };
            for (int k = 0; k < 4; k++) F(c, whole[k]) = (float)I(c, whole[k]);
            c->hold = 0;
            if (i == 0) { c->rec[0x4c / 4] |= 1; const float forever = 1e9f; memcpy(&c->rec[1], &forever, 4); }       /* (the game's driver drives the player's car, as in the recording) */
        }
        R.clock = (float)F * DT;
        R.steps = (uint32_t)F;
        R.running = 1;
        for (int i = 0; i < R.n; i++) R.off[i] = R.still[i] = 0;
        ncalls = 0;
        s2car_sparks = hook_record;
        struct s2car_input in = { 0 };
        race_step(&in);
        s2car_sparks = 0;
        const struct spark_event *ev = &events[e];
        int best = -1, late = 0;
        float bd = 1e30f;
        for (int stp = 0; stp < 2 && best < 0; stp++) {        /* (the game's step is longer than the firmware's: a touch at the end of one comes a step later) */
            if (stp) { s2car_sparks = hook_record; ncalls = 0; race_step(&in); s2car_sparks = 0; late = 1; }
            for (int q = 0; q < ncalls; q++) {
                if (calls[q].set != ev->set) continue;
                const float dx = calls[q].at[0] - ev->at[0], dz = calls[q].at[2] - ev->at[2], d = sqrtf(dx * dx + dz * dz);
                if (d < bd) { bd = d; best = q; }
            }
        }
        ev_n++;
        ev_late += late && best >= 0;
        ev_matched += best >= 0;
        if (best < 0) { printf("  frame %d: the game threw %d sparks of set %d, neither the firmware's step from the frame before nor the next one threw any of that set (%d calls)\n", F, ev->n, ev->set, ncalls); continue; }
        int n = (int)(calls[best].hit * 3600.0f / 400000.0f * 30.0f / 60.0f);
        if (n > 30) n = 30;
        ev_n_ok += n == ev->n;
        ev_n_near += abs(n - ev->n) <= 2;
        ev_at += bd < 400.0f;
        const float ddx = calls[best].dir[0] - ev->dir0, ddz = calls[best].dir[2] - ev->dir2;
        ev_dir += sqrtf(ddx * ddx + ddz * ddz) < 600.0f;
        if (e < 12 || n != ev->n) printf("  frame %d set %d: game %d sparks at %.0f %.0f %.0f direction %.0f %.0f; firmware %d at %.0f %.0f %.0f direction %.0f %.0f (%.0f apart)\n", F, ev->set, ev->n, ev->at[0], ev->at[1], ev->at[2],
                                          ev->dir0, ev->dir2, n, calls[best].at[0], calls[best].at[1], calls[best].at[2], calls[best].dir[0], calls[best].dir[2], bd);
    }
    printf("batches of sparks re-run by the firmware's race from the frame before: %d; the firmware threw sparks of the set in %d of them (%d of those a step later), the number equal in %d, within two in %d, the place within 400 units in %d, the direction within 600 units a second in %d\n", ev_n, ev_matched, ev_late, ev_n_ok, ev_n_near, ev_at, ev_dir);
    CHECK(ev_matched * 10 >= ev_n * 8 && ev_n_near * 10 >= ev_matched * 9 && ev_at * 10 >= ev_matched * 9, "the firmware's sparks");
    printf("%d frames, %d of them with an obstacle flying, %d frames missing in the recording (their states taken from the game)\n", rec.n, flying_frames, gaps);
    printf("obstacles: %d states compared, %d equal (a place within %.2f, a speed within %.1f); the firmware knocked the game's %d obstacles (%d at the game's frame); %d states put right from the game's\n",
           cone_states, cone_ok, (double)max_pos, (double)max_vel, hits_game, hits_ok, resync_cones);
    printf("%d of the %d obstacles the recorder put on the cars' ways were seen to move\n", pokes_seen, npokes);
    printf("obstacles' object records: %d compared with the game's floats (place cut, angles in 2,048ths), %d equal, %d of them exactly (the others one unit of an angle off: a sum in float32 against the x87's)\n", obj_all, obj_ok, obj_exact);
    printf("sparks: %d states compared in the flight, %d equal (a place within %.2f); %d thrown (%d batches); the chance equals the game's generator's in %d, the directions share one in %d; %d wall batches, %d with a number a car's speed gives; %d near a car's corner\n",
           spark_states, spark_ok, (double)max_spark_pos, spawns, spawn_batches, spawn_rand, spawn_dir, spawn_n, spawn_n_ok, spawn_near);
    int cov[5];
    s2props_coverage(cov);
    printf("the branches of the obstacles' flight, all of them followed to the game's frames: %d steps off the ground (%d against a wall, %d with no wall), %d landings, %d stops on the ground\n", cov[0], cov[1], cov[2], cov[3], cov[4]);
    CHECK(stand_ok == stand_all, "standing");
    CHECK(cone_ok == cone_states, "obstacle states");
    CHECK(hits_ok == hits_game, "knocks");
    CHECK(spark_ok == spark_states, "spark states");
    CHECK(spawn_rand == spawn_batches && spawn_dir == spawn_batches, "spark chance");
    CHECK(obj_ok == obj_all, "object records");
    CHECK(resync_cones == 0, "the firmware followed the game's obstacles without being put right: %d times it was", resync_cones);
    return 0;
}

/* ------------------------------------------------------------------------------------------------ pictures */

static struct track_cam camera_of(const uint8_t *fr)
{
    const uint8_t *c = fr + HEAD + CARS * REC + PROP * (NPROPS + 60);
    return (struct track_cam){ (float)i32(c, 0x48), (float)i32(c, 0x50), (float)i32(c, 0x4C), f32(c, 0x58), f32(c, 0x54), f32(c, 0x5C) };
}

/* the scene of a frame as the firmware draws it: the cars as the records pose them, the obstacles and sparks as the game's records have them (with the props or, `props` 0, without),
   from the recorded camera, into <prefix><name>_<frame>.rgb (320 x 200 x 3) */
static void render_frame(int fi, const char *prefix, const char *name, int props)
{
    const uint8_t *fr = frame_at(fi);
    static struct s2car_state cars[CARS];
    struct s2car_state *cp[CARS];
    for (int i = 0; i < CARS; i++) { state_of(&cars[i], cars_at(fr) + REC * i); cp[i] = &cars[i]; }
    for (int i = 0; i < s2props_obstacles(); i++) { struct s2props_rec g; rec_of(&g, props_at(fr, 0, i)); s2props_set(0, i, &g); }
    for (int i = 0; i < 60; i++) { struct s2props_rec g; rec_of(&g, props_at(fr, 1, i)); s2props_set(1, i, &g); }
    static struct track_object objs[ROAD_CARS + S2PROPS_MAX];
    struct track_cam cam = camera_of(fr);
    int no = road_race_scene(cp, CARS, &cam, objs);
    cam = camera_of(fr);
    if (props) no += s2props_objects(&cam, objs + no, ROAD_CARS + S2PROPS_MAX - no);
    const struct rs_clip clip = { 0, 0, FB_W, FB_H };
    volatile uint8_t *fb = FB(0);
    if (props && getenv("COST")) {                      /* what the props' pieces cost the units: the pieces in view (the drawing's own cull: the objects out of view are not made), their faces and the pixels they fill */
        struct su_view v;
        track_view(&clip, &v);
        uint32_t px = 0, pieces = 0;
        struct gu_stats gst = { 0, 0, 0, 0 };
        float f[3], r[3], u[3];
        track_cam_axes(&cam, f, r, u);
        for (int k = ROAD_CARS; k < no; k++) {
            struct track_object *ob = &objs[k];
            const float dx = ob->x - cam.x, dy = ob->y - cam.y, dh = ob->h - cam.h, rad = ob->radius;
            const float vf = dx * f[0] + dy * f[1] + dh * f[2] + 128.0f, vr = dx * r[0] + dy * r[1] + dh * r[2], vu = dx * u[0] + dy * u[1] + dh * u[2];
            const float hw = 160.0f / 128.0f;           /* (the lens: about the picture's half width over the focal length; the cull is the drawing's, this one is a little wider) */
            if (vf + rad < 128.0f || vf - rad > track_far() + 128.0f || fabsf(vr) - rad > hw * (vf + rad) || fabsf(vu) - rad > hw * (vf + rad)) continue;
            if (ob->make) ob->make(ob->id, &cam, ob);
            for (int b = 0; b < ob->n; b++) {
                struct gu_piece g;
                track_block_piece(&cam, &ob->b[b], &g);
                px += gu_draw(FB(1), &clip, &v, track_mlb(), &g, &gst);
                pieces++;
            }
        }
        printf("frame %d props cost: %u pieces, %u faces, %u triangles, %u pixels\n", fi, pieces, gst.faces, gst.triangles, px);
    }
    if (getenv("COSTONLY")) return;                    /* (only the count above) */
    static struct blit_list l;
    blit_start(&l, 0);
    track_backdrop_blit(&l, &clip, &cam);
    blit_model(fb, &l);
    struct track_stats ts;
    track_mode = TRACK_MODEL;
    track_draw_cam(fb, &clip, &cam, objs, no, &ts);
    char path[600];
    snprintf(path, sizeof path, "%s%s_%d.rgb", prefix, name, fi);
    FILE *f = fopen(path, "wb");
    const uint32_t *pal = track_palette();
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(pal[fb[i]] >> 16), (uint8_t)(pal[fb[i]] >> 8), (uint8_t)pal[fb[i]] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("frame %d %s: %d objects, %u pieces, %u faces, %u triangles\n", fi, name, no, ts.pieces, ts.faces, ts.triangles);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: check_props_s2 model <image> <track> <steps> | rec <image> <track> <recording> <frame bytes> | render <image> <track> <recording> <frame bytes> <out prefix> <frames>\n"); return 2; }
    map();
    const int track = atoi(argv[3]);
    if (!open_image(argv[2])) { fprintf(stderr, "the image does not open\n"); return 2; }
    struct s2car_state boot;
    if (!s2car_init(&boot, 0, 0, track, 0)) { fprintf(stderr, "the car's files (S2_GAME) are not there\n"); return 2; }
    if (!strcmp(argv[1], "render") && argc > 6) {         /* render <image> <track> <recording> <frame bytes> <out prefix> <frame>[-<frame>]... */
        size_t rn;
        rec.d = slurp(argv[4], &rn);
        rec.frame = (size_t)atoi(argv[5]);
        rec.n = (int)(rn / rec.frame);
        struct track_fx fx;
        if (track_fx(&fx)) rs_smg = fx.table;
        load_props(track);
        s2props_begin();
        race_step_hook = 0;
        s2car_sparks = 0;
        for (int a = 7; a < argc; a++) {
            int first = atoi(argv[a]), last = first;
            const char *dash = strchr(argv[a], '-');
            if (dash) last = atoi(dash + 1);
            for (int f = first; f <= last && f < rec.n; f++) {
                render_frame(f, argv[6], "with", 1);
                render_frame(f, argv[6], "nofx", 0);
            }
        }
        return 0;
    }
    if (!strcmp(argv[1], "rec")) record(track, argv[4], argc > 5 ? atoi(argv[5]) : 0);
    if (!strcmp(argv[1], "model")) model(track, atoi(argv[4]));
    printf("%d checks, %d failed\n", checks, failures);
    printf(failures ? "FAIL\n" : "PASS\n");
    return failures != 0;
}
