/*
 * Host test of the race's replay (menus row 10: firmware/race.c, s2ghost.c; the camera's in firmware/s2cam.c), run by tools/s2/check_replay_s2.sh: the firmware's own
 * code on the PC (the physics, the race, the track image of tools/s2/make_track_s2.py), the SDRAM mapped at its addresses, the game's files for the car's numbers
 * (S2_GAME). It builds on tools/s2/check_ghost_s2.c (the pilot, the image files, the CHECK macro), which it includes.
 *
 * On each of the seven tracks the arcade's race (four cars on the grid, one lap, the player a pilot with a driver's swerves, the gearbox switched, the handbrake and
 * the brake now and then, so that the cars about him are touched) is run in frames of one to four steps, as the viewer takes them when a frame is late, and every
 * car's whole record is kept after every step. Then:
 *
 *   batching    the same controls in frames of other sizes (one step, two to four, one step again after a late one) give the same race at every step. (The order the
 *               viewer used to take the steps in, all of a frame's player steps and then all its computers', is another race from the first late frame on: counted.)
 *   recorded    the recorder (s2ghost.c) holds the controls up to the step in which the player's car had done its laps, as the bytes the race was driven with
 *   replay      the race run again with race_replay set, the player's controls asked for from nobody (garbage is handed in), equals the recorded race in all four cars at
 *               every step, the steps after the finish too (the driver takes the car); the recording is as it was; in other frames, after the cars' gearbox flags are
 *               left as an earlier ghost leaves them, after another race (the free run) has been run on the track, and a second time
 *   stop        ESC in the replay: the race stops at that step, the player's car is done, the clock and the place stay; a second ESC changes nothing; a replay of a
 *               race that was not run to the end is not offered (race_replay_ok)
 *   long        a recording over S2GH_MAX steps is not a replay: race_init sets up an ordinary race
 *   set-ups     every one of the above again with a CUSTOMIZE set-up for the player's car and the championship's tuned computers (race_custom, race_setup, race_tuned): the replay
 *               takes the recorded race's set-up, not the viewer's globals of the moment (they are changed before it begins); a negative control: the same race with the
 *               standard set-up is another race
 *   cameras     the trackside cameras of every track (CAMERASn.MCM, 23 to 34 cameras of all five kinds), a car driven along the polyline of the cameras' places at a speed
 *               and in a loop: the camera of every frame (its place, angles and the camera in use) against a model of STARTL.EXE's routines written again in double precision
 *               (the firmware's are single precision: places within 2 units, angles within 6 milliradians), the choice of the camera cutting along the track as the model
 *
 * Exit status 1 if any check fails.
 */
#define main check_ghost_main
#include "check_ghost_s2.c"
#undef main

/* ------------------------------------------------------------------------------------------------ the arcade's race, every car's record whole */

static uint32_t hash_all(const struct s2car_state *c)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof c->rec / 4; i++) {
        h ^= c->rec[i];
        h *= 16777619u;
    }
    return h;
}

struct world { uint32_t h[RACE_CARS]; };

static void world_take(struct world *w)
{
    for (int i = 0; i < RACE_CARS; i++) w->h[i] = hash_all(race_car(i));
}

static int image(const char *dir, int track)
{
    char path[512];
    snprintf(path, sizeof path, "%s/car%d.bin", dir, track);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    return n > 1000 && track_open(TEX_FILE) && track_init() && road_build() > 0;
}

#define NSTEPS 14000
static uint8_t ctl[NSTEPS];                             /* the controls of the recorded race as the recorder's bytes */
static struct world rec_world[NSTEPS], other[NSTEPS];
static uint32_t finish_k;                               /* the step in which the player's car had done its laps (a count of steps, 1 up), 0: not in this run */
static const int ONE[1] = { 1 }, LATE[8] = { 1, 1, 2, 1, 3, 1, 4, 1 }, MANY[3] = { 2, 3, 4 };

static int t_custom, t_tuned;                           /* the set-ups of the runs in hand: the player's CUSTOMIZE one (or the standard), the computers tuned or not */
static struct s2car_setup t_setup;

static void setup(int laps)
{
    race_custom = t_custom;
    race_setup = t_setup;
    race_tuned = t_tuned;
    race_kind = RACE_NORMAL;
    race_ghost = 0;
    race_laps = laps;
    race_level = 1;
    race_grid[0] = (struct s2_pick){ 1, 1 };
    race_grid[1] = (struct s2_pick){ 2, 2 };
    race_grid[2] = (struct s2_pick){ 3, 3 };
    race_grid[3] = (struct s2_pick){ 4, 4 };
}

/* The race the viewer runs: frames of the plan's sizes, each step the player's and the computers' (viewer.c), the player a pilot with his swerves; the controls into ctl[]
   (the recorder's byte), the world after each step into w[]. Runs until `after` steps beyond the finish or nsteps. Returns the steps run. */
static uint32_t record_race(int track, int laps, int manual0, uint32_t nsteps, uint32_t after, const int *plan, int nplan, struct world *w)
{
    struct pilot pl = { -1e9f, 85.0f, PILOT_SIGN };
    setup(laps);
    race_replay = 0;
    race_car(0)->manual = manual0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    rng_state = 4242 + (uint32_t)track;
    finish_k = 0;
    uint32_t k = 0;
    for (int f = 0; k < nsteps; f++) {
        int n = plan[f % nplan];
        if ((uint32_t)n > nsteps - k) n = (int)(nsteps - k);
        for (int b = 0; b < n; b++) {
            struct s2car_input in = pilot_input(&pl, race_car(0), 0);
            const uint32_t r = rnd();
            if (r % 400 == 0) race_car(0)->manual = !race_car(0)->manual;
            if ((r >> 10) % 300 == 0) in.handbrake = 1;
            if ((r >> 14) % 500 == 0) in.brake = 1;
            if ((k + (uint32_t)b) % 700 > 600) in.steer = (float)(int)((r >> 16) % 3) - 1.0f;       /* (a swerve: the cars about him are touched now and then) */
            if (race_car(0)->manual) in.gear_up = (r >> 4) % 90 == 0, in.gear_down = (r >> 7) % 120 == 0;
            ctl[k + (uint32_t)b] = s2gh_pack(&in, race_car(0)->manual);
            race_step_player(&in);
            race_step_others(1);
            world_take(&w[k + (uint32_t)b]);
            if (!finish_k && (race_car(0)->rec[0x4c / 4] & 1)) finish_k = k + (uint32_t)b + 1;
        }
        k += (uint32_t)n;
        if (finish_k && k >= finish_k + after) break;
    }
    return k;
}

/* The same race from the bytes of ctl[] by hand, in the viewer's order (new_order) or its old one; a snapshot per step (new) or per frame end (old) */
static void race_by_hand(int track, int laps, int manual0, uint32_t nsteps, const int *plan, int nplan, int new_order, struct world *w)
{
    setup(laps);
    race_replay = 0;
    race_car(0)->manual = manual0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    uint32_t k = 0;
    for (int f = 0; k < nsteps; f++) {
        int n = plan[f % nplan];
        if ((uint32_t)n > nsteps - k) n = (int)(nsteps - k);
        for (int b = 0; b < n; b++) {
            int manual;
            struct s2car_input in = s2gh_unpack(ctl[k + (uint32_t)b], &manual);
            race_car(0)->manual = manual;
            race_step_player(&in);
            if (new_order) {
                race_step_others(1);
                world_take(&w[k + (uint32_t)b]);
            }
        }
        if (!new_order) {
            race_step_others((uint32_t)n);
            world_take(&w[k + (uint32_t)n - 1]);
        }
        k += (uint32_t)n;
    }
}

/* The replay: race_replay set, the player asked for nothing (garbage controls are handed in, as a viewer's keys would be); stop_at: ESC at that step (0: never) */
static uint32_t replay_race(int track, int laps, uint32_t nsteps, const int *plan, int nplan, uint32_t stop_at, struct world *w)
{
    setup(laps);
    race_replay = 1;
    race_custom = !t_custom;                                /* what the viewer's globals may be by the time of the replay: another set-up; the replay is of the recorded race's */
    race_setup = (struct s2car_setup){ 1, 3, 2, 0, 4, 1 };
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init for the replay", track);
    CHECK(race_replaying() && race_replay == 0, "track %d: the race is a replay and the request is used up", track);
    uint32_t k = 0;
    for (int f = 0; k < nsteps; f++) {
        int n = plan[f % nplan];
        if ((uint32_t)n > nsteps - k) n = (int)(nsteps - k);
        for (int b = 0; b < n; b++) {
            const uint32_t r = rnd();
            struct s2car_input junk = { .throttle = (float)(r & 1), .brake = (float)(r >> 1 & 1), .steer = (float)(int)((r >> 2) % 3) - 1.0f, .gear_up = (int)(r >> 5 & 1),
                                        .gear_down = (int)(r >> 6 & 1), .handbrake = (int)(r >> 7 & 1) };
            if (stop_at && k + (uint32_t)b == stop_at) race_replay_stop();
            race_step_player(&junk);
            race_step_others(1);
            world_take(&w[k + (uint32_t)b]);
        }
        k += (uint32_t)n;
    }
    return k;
}

static uint32_t first_difference(const struct world *a, const struct world *b, uint32_t n, uint32_t *bad)
{
    uint32_t first = 0;
    *bad = 0;
    for (uint32_t k = 0; k < n; k++)
        if (memcmp(&a[k], &b[k], sizeof a[k])) {
            if (!*bad) first = k;
            (*bad)++;
        }
    return first;
}

static void check_track(const char *dir, int track)
{
    const int manual0 = track & 1, laps = 1;
    uint32_t bad, first;
    CHECK(image(dir, track), "track %d: the image", track);
    printf("track %d\n", track);

    /* the race as the viewer runs it, in late frames now and then */
    const uint32_t n = record_race(track, laps, manual0, NSTEPS, 600, LATE, 8, rec_world);
    CHECK(finish_k > 0, "track %d: the pilot did not finish the lap in %u steps", track, n);
    const uint32_t steps_rec = s2gh_rec_steps();
    race_by_hand(track, laps, manual0, n, ONE, 1, 1, other);
    first = first_difference(rec_world, other, n, &bad);
    CHECK(bad == 0, "track %d: the same controls a step at a time are the same race: %u of %u steps differ (the first %u)", track, bad, n, first);
    race_by_hand(track, laps, manual0, n, MANY, 3, 1, other);
    first = first_difference(rec_world, other, n, &bad);
    CHECK(bad == 0, "track %d: frames of 2, 3 and 4 steps are the same race: %u of %u steps differ (the first %u)", track, bad, n, first);
    race_by_hand(track, laps, manual0, n, LATE, 8, 0, other);
    uint32_t differ = 0, ends = 0, first_old = 0;
    for (uint32_t k = 0, f = 0; k < n; f++) {
        k += (uint32_t)LATE[f % 8];
        if (k > n) break;
        ends++;
        if (memcmp(&rec_world[k - 1], &other[k - 1], sizeof rec_world[0])) { if (!differ) first_old = k - 1; differ++; }
    }

    /* what the recorder holds */
    CHECK(steps_rec == finish_k, "track %d: %u steps recorded, the car had done its laps in step %u", track, steps_rec, finish_k);
    CHECK(!memcmp(s2gh_rec_data(), ctl, steps_rec), "track %d: the recorded bytes are the controls the race was driven with", track);
    CHECK(!s2gh_rec_full(), "track %d: the recording is whole", track);
    CHECK(race_replay_ok(), "track %d: the race just run can be run again", track);
    struct race_hud h0;
    race_get_hud(&h0);
    uint8_t copy[NSTEPS];
    memcpy(copy, s2gh_rec_data(), steps_rec);

    /* the replay: in a step at a time, in late frames, in frames of 2 .. 4 */
    const int *plans[3] = { ONE, LATE, MANY };
    const int nplans[3] = { 1, 8, 3 };
    for (int p = 0; p < 3; p++) {
        replay_race(track, laps, n, plans[p], nplans[p], 0, other);
        first = first_difference(rec_world, other, n, &bad);
        CHECK(bad == 0, "track %d: the replay (plan %d) equals the race: %u of %u steps differ (the first %u)", track, p, bad, n, first);
        CHECK(s2gh_rec_steps() == steps_rec && !memcmp(s2gh_rec_data(), copy, steps_rec), "track %d: the replay leaves the recording as it was", track);
        CHECK(race_replay_ok(), "track %d: the replay can be run again", track);
    }
    struct race_hud h1;
    race_get_hud(&h1);
    CHECK(h1.finished && h1.time == h0.time && h1.place == h0.place && h1.best_lap == h0.best_lap, "track %d: the replay ends as the race did (time %.3f / %.3f, place %d / %d)", track,
          (double)h1.time, (double)h0.time, h1.place, h0.place);

    /* what an earlier race leaves behind: the computers' gearbox flags as an earlier ghost leaves them, the player's, a free run on the track */
    for (int i = 0; i < RACE_CARS; i++) race_car(i)->manual = 1;
    race_kind = RACE_FREE;
    race_laps = 1;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: a free run", track);
    for (int k = 0; k < 300; k++) {
        struct s2car_input in = { .throttle = 1.0f, .steer = (float)((k / 50) % 3) - 1.0f };
        race_step_player(&in);
        race_step_others(1);
    }
    for (int i = 0; i < RACE_CARS; i++) race_car(i)->manual = 1;
    CHECK(!race_replay_ok(), "track %d: a free run cannot be run again", track);
    replay_race(track, laps, n, LATE, 8, 0, other);
    first = first_difference(rec_world, other, n, &bad);
    CHECK(bad == 0, "track %d: the replay after another race equals the race: %u of %u steps differ (the first %u)", track, bad, n, first);

    /* ESC in the replay */
    const uint32_t stop = finish_k / 2;
    replay_race(track, laps, stop + 300, LATE, 8, stop, other);
    first = first_difference(rec_world, other, stop, &bad);
    CHECK(bad == 0, "track %d: the replay stopped at step %u equals the race up to it (%u differ)", track, stop, bad);
    CHECK(race_replaying() && race_replay_ok(), "track %d: a stopped replay has its end screen (the player's car is done)", track);
    struct race_hud hs;
    race_get_hud(&hs);
    CHECK(hs.finished && hs.time > 0.0f && hs.time < h0.time, "track %d: the stopped replay's clock is the time so far (%.2f s of %.2f)", track, (double)hs.time, (double)h0.time);
    uint32_t still = 1;
    for (uint32_t k = stop + 1; k < stop + 300; k++) still &= !memcmp(&other[k], &other[stop], sizeof other[0]);
    CHECK(still, "track %d: after ESC the race stands still", track);
    race_replay_stop();
    struct race_hud hs2;
    race_get_hud(&hs2);
    CHECK(!memcmp(&hs, &hs2, sizeof hs), "track %d: a second ESC changes nothing", track);
    replay_race(track, laps, n, ONE, 1, 0, other);
    first = first_difference(rec_world, other, n, &bad);
    CHECK(bad == 0, "track %d: the replay after a stopped one equals the race again: %u of %u steps differ", track, bad, n);

    printf("  track %d%s: %u steps in frames of 1 .. 4 are the one-step race, the player done in step %u; the replay (3 frame plans, after another race, after ESC) equals it in all four cars\n"
           "    at every step; the old order (a frame's player steps, then its computers') differs at %u of %u frame ends (the first at step %u)\n",
           track, t_custom ? " with a set-up and tuned computers" : "", n, finish_k, differ, ends, first_old);

    /* a negative control: the same controls with the other set-up are another race (the set-up is part of the race) */
    if (t_custom) {
        static struct world other_set[NSTEPS];
        const int c = t_custom, tu = t_tuned;
        t_custom = 0;
        t_tuned = 0;
        race_by_hand(track, laps, manual0, n, ONE, 1, 1, other_set);
        t_custom = c;
        t_tuned = tu;
        uint32_t d = 0;
        for (uint32_t k = 0; k < n; k++) d += memcmp(&rec_world[k], &other_set[k], sizeof rec_world[0]) != 0;
        CHECK(d > n / 2, "track %d: the same controls with the standard set-up and computers are another race (%u of %u steps differ)", track, d, n);
        printf("    negative control: the standard set-up and computers give another race in %u of %u steps\n", d, n);
    }
}

/* a replay is offered only for a race that was run to its end, and a long recording is not one */
static void limits(void)
{
    setup(3);
    race_replay = 0;
    CHECK(race_init(1, road_floor() - 3000.0f), "limits: race_init");
    for (int k = 0; k < 400; k++) {
        struct s2car_input in = { .throttle = 1.0f };
        race_step_player(&in);
        race_step_others(1);
    }
    CHECK(s2gh_rec_steps() == 400 && !race_replay_ok(), "a race not yet run to its end cannot be run again");
    /* a recording over the limit */
    s2gh_rec_begin(1, 1, 1, 0, 0, 0, 0);
    struct s2car_input in = { .throttle = 1.0f };
    for (uint32_t i = 0; i < S2GH_MAX + 10; i++) s2gh_rec_step(&in, 0);
    CHECK(s2gh_rec_full() && s2gh_rec_steps() == S2GH_MAX, "a race over %u steps fills the recording", S2GH_MAX);
    race_replay = 1;
    CHECK(race_init(1, road_floor() - 3000.0f) && !race_replaying(), "a recording that is full is not a replay: an ordinary race is set up");
    CHECK(!s2gh_rec_full() && s2gh_rec_steps() == 0, "... and its recording begins again");
}

/* ------------------------------------------------------------------------------------------------ the cameras */

#include "s2cam.h"
#include "dat.h"

struct mrow { int32_t x, h, z; float pitch, heading, roll; int32_t kind; float figure; };
static struct { struct mrow row[80]; int n; int shot, last; int32_t x, h, z; double pitch, yaw, roll, turn, rise; } M;

static double m_angle(double x, double y)                           /* 0x10d9e */
{
    double a = acos(x / sqrt(x * x + y * y));
    return y < 0 ? 6.283185307179586 - a : a;
}

static void m_look(const int32_t p[3])                              /* 0x33905: the camera (M.x, M.h, M.z) looks at the car at p */
{
    const double dx = (double)M.x - p[0], dy = (double)M.h - p[1], dz = (double)M.z - p[2];
    const double a = fabs(acos(sqrt(dx * dx + dz * dz) / sqrt(dx * dx + dy * dy + dz * dz)));
    M.yaw = m_angle(dx, dz) - 1.5707963267948966;
    M.pitch = dy < 0 ? a : 6.283185307179586 - a;
}

static void m_turn(double x, double z, double a, double out[2])    /* 0x10388 */
{
    out[0] = z * sin(a) + x * cos(a);
    out[1] = z * cos(a) - x * sin(a);
}

static void m_frame(const int32_t p[3], double speed, double dt)
{
    int best = -1, pick = M.shot;
    for (int i = 0; i < M.n; i++) {
        const double dx = (double)(M.row[i].x >> 6) - (p[0] >> 6), dz = (double)(M.row[i].z >> 6) - (p[2] >> 6), d = sqrt(dx * dx + dz * dz);
        if (d != 0.0 && ((double)best > d || best < 0)) { best = (int)d; pick = i; }
    }
    const int now = M.shot, next = now + 1, before = now - 1;
    if ((pick == next || now == -1 || (next == M.n && pick == 0) || pick == before || (before < 0 && M.n - 1 == pick)) && pick != now) M.shot = pick;
    const struct mrow *r = &M.row[M.shot];
    double t[2];
    switch (r->kind) {
    case 0: M.x = r->x; M.h = r->h; M.z = r->z; M.pitch = r->pitch; M.yaw = r->heading; M.roll = r->roll; break;
    case 1:
        if (M.shot != M.last) { M.x = r->x; M.h = r->h; M.z = r->z; }
        else if (r->figure != 0.0f) {
            const double k = (double)r->figure * speed * 111.0;
            M.x = (int32_t)(cos(M.pitch) * sin(M.yaw) * k * dt + M.x);
            M.h = (int32_t)(sin(M.pitch) * k * dt + M.h);
            M.z = (int32_t)(-cos(M.pitch) * cos(M.yaw) * k * dt + M.z);
        }
        m_look(p);
        M.roll = r->roll;
        break;
    case 2:
        for (int k = 0; k < 3; k++) {
            const int32_t rv = k == 0 ? r->x : k == 1 ? r->h : r->z;
            const int32_t v = (int32_t)((double)(rv - p[k]) * (0.8 - (double)r->figure * 0.75) + p[k]);
            if (k == 0) M.x = v; else if (k == 1) M.h = v; else M.z = v;
        }
        m_look(p);
        M.roll = r->roll;
        break;
    case 3:
        m_turn(1100.0, 0.0, M.turn * 6.283185307179586, t);
        M.x = p[0] + (int32_t)t[0];
        M.h = p[1] + (int32_t)(sin(M.rise * 12.566370614359172) * 220.0 + 440.0);
        M.z = p[2] + (int32_t)t[1];
        M.turn += ((double)r->figure * 0.5 + 0.1) * dt;
        if (M.turn > 1.0) M.turn -= 1.0;
        M.rise += ((double)r->figure * 0.5 + 0.1) * dt;
        if (M.rise > 1.0) M.rise -= 1.0;
        m_look(p);
        M.roll = 0.0;
        break;
    case 4:
        m_turn((double)(int32_t)((double)r->figure * 500.0 + 1320.0), 0.0, (double)r->heading + 3.141592653589793, t);
        M.x = p[0] + (int32_t)t[0];
        M.h = p[1] + (int32_t)((double)r->figure * 200.0 + 550.0);
        M.z = p[2] + (int32_t)t[1];
        m_look(p);
        M.roll = 0.0;
        break;
    }
    M.last = M.shot;
}

static double wrap(double a)
{
    while (a > 3.141592653589793) a -= 6.283185307179586;
    while (a < -3.141592653589793) a += 6.283185307179586;
    return a;
}

static void cameras(const char *dir)
{
    static struct s2car_state car;
    for (int track = 1; track <= 7; track++) {
        CHECK(image(dir, track), "track %d: the image", track);
        setup(1);
        CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init (the car's data)", track);
        uint32_t msize;
        const uint8_t *map = s2car_map_dat(&msize);
        char name[16] = "CAMERAS0.MCM";
        name[7] = (char)('0' + track);
        struct dat_entry e;
        static uint8_t buf[2048];
        CHECK(map && dat_find(map, msize, name, &e) && dat_extract(map, msize, &e, buf, sizeof buf) == e.size, "track %d: %s in the container", track, name);
        memset(&M, 0, sizeof M);
        memcpy(&M.n, buf, 4);
        CHECK(M.n > 0 && M.n <= 80 && e.size == 4u + 32u * (uint32_t)M.n, "track %d: %d cameras", track, M.n);
        memcpy(M.row, buf + 4, 32u * (uint32_t)M.n);
        M.shot = M.last = -1;
        s2cam_begin(track, S2CAM_TRACKSIDE);
        CHECK(s2cam_rows() == M.n && s2cam_get_view() == S2CAM_TRACKSIDE, "track %d: the firmware reads the same %d cameras", track, M.n);
        /* a car along the cameras' places, round and round at 20,000 units a second, its height following */
        const struct s2car_state *cars[1] = { &car };
        struct track_cam cam;
        int kinds_seen[5] = { 0 }, cuts = 0, last_shot = -1, bad_pos = 0, bad_ang = 0, bad_shot = 0;
        double worst_pos = 0, worst_ang = 0;
        float sp = 100.0f;
        memcpy(&car.rec[0x500 / 4], &sp, 4);
        double s = 0.0, len = 0, sx[81], sz[81], sh[81], cum[82];
        for (int i = 0; i <= M.n; i++) {
            const struct mrow *a = &M.row[i % M.n];
            sx[i] = a->x; sz[i] = a->z; sh[i] = a->h;
            cum[i] = len;
            if (i < M.n) {
                const struct mrow *b = &M.row[(i + 1) % M.n];
                len += sqrt(((double)b->x - a->x) * ((double)b->x - a->x) + ((double)b->z - a->z) * ((double)b->z - a->z));
            }
        }
        const int frames = 6000;
        for (int f = 0; f < frames; f++) {
            s += 0.02 * 20000.0;
            const double d = fmod(s, len);
            int seg = 0;
            while (seg < M.n - 1 && cum[seg + 1] <= d) seg++;
            const double u = (d - cum[seg]) / (cum[seg + 1] - cum[seg] > 0 ? cum[seg + 1] - cum[seg] : 1.0);
            car.pos[0] = (float)(int32_t)(sx[seg] + (sx[seg + 1] - sx[seg]) * u);
            car.pos[1] = (float)(int32_t)(sz[seg] + (sz[seg + 1] - sz[seg]) * u + 300.0);       /* (beside the road a little: never exactly on a camera) */
            car.pos[2] = (float)(int32_t)(sh[seg] + (sh[seg + 1] - sh[seg]) * u - 200.0);
            const int32_t p[3] = { (int32_t)car.pos[0], (int32_t)car.pos[2], (int32_t)car.pos[1] };
            if (f) {                                                /* each frame from the camera the firmware had: a camera that passes through the car would turn the two apart (the angles there are all but undefined) */
                M.x = (int32_t)cam.x; M.h = (int32_t)cam.h; M.z = (int32_t)cam.y;
                M.pitch = cam.pitch; M.yaw = cam.yaw;
            }
            m_frame(p, (double)sp, 0.02);
            s2cam_frame(cars, 1, 0.02f, &cam);
            const struct mrow *r = &M.row[M.shot];
            if (s2cam_shot() != M.shot && bad_shot++ < 3) printf("    track %d frame %d: shot %d, the model's %d\n", track, f, s2cam_shot(), M.shot);
            kinds_seen[r->kind]++;
            if (M.shot != last_shot) cuts++;
            last_shot = M.shot;
            const double dp = fmax(fabs((double)cam.x - M.x), fmax(fabs((double)cam.y - M.z), fabs((double)cam.h - M.h)));
            const double da = fmax(fabs(wrap((double)cam.yaw - M.yaw)), fmax(fabs(wrap((double)cam.pitch - wrap(M.pitch))), fabs((double)cam.roll - M.roll)));
            if (dp > worst_pos) worst_pos = dp;
            if (da > worst_ang) worst_ang = da;
            const double dist = sqrt(((double)M.x - p[0]) * ((double)M.x - p[0]) + ((double)M.h - p[1]) * ((double)M.h - p[1]) + ((double)M.z - p[2]) * ((double)M.z - p[2]));
            if (r->kind != 3 && dp > 2.5) bad_pos++;                         /* (the circling camera's two turns are floats against doubles: a few units after thousands of frames) */
            if (r->kind != 3 && da > 0.006 + 3.0 / (dist > 1.0 ? dist : 1.0)) bad_ang++;
        }
        CHECK(bad_pos == 0, "track %d: %d frames of the camera's place differ from the model by more than 2 units (worst %.1f)", track, bad_pos, worst_pos);
        CHECK(bad_ang == 0, "track %d: %d frames of the camera's angles differ from the model by more than 6 milliradians (worst %.4f)", track, bad_ang, worst_ang);
        CHECK(bad_shot == 0, "track %d: the camera in use differs from the model's in %d frames", track, bad_shot);
        CHECK(cuts >= M.n / 2, "track %d: the camera cuts along the track (%d cuts)", track, cuts);
        int present[5] = { 0 };
        for (int i = 0; i < M.n; i++) present[M.row[i].kind] = 1;
        for (int k = 0; k < 5; k++) CHECK(!present[k] || kinds_seen[k] > 0, "track %d: a camera of kind %d is in the file and was never in use", track, k);
        printf("  track %d: %d cameras, %d cuts in %d frames, kinds in use %d %d %d %d %d, worst place %.1f units, worst angle %.4f rad\n", track, M.n, cuts, frames,
               kinds_seen[0], kinds_seen[1], kinds_seen[2], kinds_seen[3], kinds_seen[4], worst_pos, worst_ang);
    }
}

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    if (argc < 2) return 2;
    for (int track = 1; track <= 7; track++) check_track(argv[1], track);
    static const struct s2car_setup sets[7] = { { 4, 0, 1, 1, 4, 0 }, { 0, 4, 2, 4, 0, 4 }, { 3, 1, 0, 3, 1, 3 }, { 1, 3, 1, 0, 2, 4 }, { 3, 2, 2, 3, 1, 3 }, { 0, 0, 0, 0, 0, 0 }, { 2, 2, 0, 2, 2, 2 } };
    for (int track = 1; track <= 7; track++) {              /* the same with a set-up of CUSTOMIZE for the player and the championship's computers */
        t_custom = t_tuned = 1;
        t_setup = sets[track - 1];
        check_track(argv[1], track);
    }
    t_custom = t_tuned = 0;
    CHECK(image(argv[1], 1), "image of track 1");
    limits();
    printf("cameras\n");
    cameras(argv[1]);
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
