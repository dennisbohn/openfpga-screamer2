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

static void setup(int laps)
{
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

    printf("  track %d: %u steps in frames of 1 .. 4 are the one-step race, the player done in step %u; the replay (3 frame plans, after another race, after ESC) equals it in all four cars\n"
           "    at every step; the old order (a frame's player steps, then its computers') differs at %u of %u frame ends (the first at step %u)\n",
           track, n, finish_k, differ, ends, first_old);
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
    s2gh_rec_begin(1, 1, 1, 0, 0);
    struct s2car_input in = { .throttle = 1.0f };
    for (uint32_t i = 0; i < S2GH_MAX + 10; i++) s2gh_rec_step(&in, 0);
    CHECK(s2gh_rec_full() && s2gh_rec_steps() == S2GH_MAX, "a race over %u steps fills the recording", S2GH_MAX);
    race_replay = 1;
    CHECK(race_init(1, road_floor() - 3000.0f) && !race_replaying(), "a recording that is full is not a replay: an ordinary race is set up");
    CHECK(!s2gh_rec_full() && s2gh_rec_steps() == 0, "... and its recording begins again");
}

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    if (argc < 2) return 2;
    for (int track = 1; track <= 7; track++) check_track(argv[1], track);
    CHECK(image(argv[1], 1), "image of track 1");
    limits();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
