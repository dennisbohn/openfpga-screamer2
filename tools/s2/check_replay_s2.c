/*
 * Host test of the race's replay (menus row 10: firmware/race.c, s2ghost.c, s2cam.c), run by tools/s2/check_replay_s2.sh: the firmware's own code on the PC (the physics,
 * the race, the track image of tools/s2/make_track_s2.py), the SDRAM mapped at its addresses, the game's files for the car's numbers (S2_GAME). It builds on
 * tools/s2/check_ghost_s2.c (the pilot, the snapshots, the image loading), which it includes.
 *
 *   batching    the arcade's race, four cars on the grid, taken in frames of one to four steps, each step the player's and then the computers' (the order viewer.c
 *               takes them in): the same race, every car's whole record equal at every step. (The order the viewer used to take them in, all of a frame's player steps
 *               and then all its computers', is another race as soon as a frame is late: that is counted too.)
 *   replay      ...
 *
 * Exit status 1 if any check fails.
 */
#define main check_ghost_main
#include "check_ghost_s2.c"
#undef main

/* ------------------------------------------------------------------------------------------------ the arcade's race, a car's record whole */

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

#define NSTEPS 6000
static uint8_t ctl[NSTEPS];                             /* the controls of a race as the recorder's bytes */

/* The controls: the pilot (steering to a point of the road ahead) with a random burst now and then, the gearbox switched, as a player would; the bytes. */
static void make_controls(int track, int manual0)
{
    struct pilot pl = { -1e9f, 85.0f, PILOT_SIGN };
    race_kind = RACE_NORMAL;
    race_laps = 3;
    race_level = 1;
    race_car(0)->manual = manual0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    rng_state = 4242 + (uint32_t)track;
    for (int k = 0; k < NSTEPS; k++) {
        struct s2car_input in = pilot_input(&pl, race_car(0), 0);
        const uint32_t r = rnd();
        if (r % 400 == 0) race_car(0)->manual = !race_car(0)->manual;
        if ((r >> 10) % 300 == 0) in.handbrake = 1;
        if ((r >> 14) % 500 == 0) in.brake = 1;
        if (k % 700 > 600) in.steer = (float)(int)((r >> 16) % 3) - 1.0f;           /* (a swerve: the cars about it touch now and then) */
        if (race_car(0)->manual) in.gear_up = (r >> 4) % 90 == 0, in.gear_down = (r >> 7) % 120 == 0;
        ctl[k] = s2gh_pack(&in, race_car(0)->manual);
        race_step_player(&in);
        race_step_others(1);
    }
}

/* A race from `ctl` (the bytes of the pack), frames of the plan's sizes (cycled); new: each step the player's and then the computers' (viewer.c), else all the frame's
   player steps and then all its computers' (the viewer's old order); a snapshot of the world after each step (new) or each frame (old) into w[step]. */
static uint32_t race_from_controls(int track, int manual0, const int *plan, int nplan, int new_order, struct world *w)
{
    race_kind = RACE_NORMAL;
    race_laps = 3;
    race_level = 1;
    race_car(0)->manual = manual0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    uint32_t k = 0;
    for (int f = 0; k < NSTEPS; f++) {
        int n = plan[f % nplan];
        if ((uint32_t)n > NSTEPS - k) n = (int)(NSTEPS - k);
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
    return k;
}

static void batching(int track)
{
    static struct world a[NSTEPS], b[NSTEPS], c[NSTEPS];
    static const int one[1] = { 1 };
    static const int late[8] = { 1, 1, 2, 1, 3, 1, 4, 1 };
    static const int all[3] = { 2, 3, 4 };
    const int manual0 = track & 1;
    make_controls(track, manual0);
    race_from_controls(track, manual0, one, 1, 1, a);
    race_from_controls(track, manual0, late, 8, 1, b);
    race_from_controls(track, manual0, all, 3, 1, c);
    uint32_t bad_b = 0, bad_c = 0, first_b = 0, first_c = 0;
    for (int k = 0; k < NSTEPS; k++) {
        if (memcmp(&a[k], &b[k], sizeof a[k])) { if (!bad_b) first_b = (uint32_t)k; bad_b++; }
        if (memcmp(&a[k], &c[k], sizeof a[k])) { if (!bad_c) first_c = (uint32_t)k; bad_c++; }
    }
    CHECK(bad_b == 0, "track %d: frames of 1 to 4 steps are the same race: %u of %d steps differ (the first %u)", track, bad_b, NSTEPS, first_b);
    CHECK(bad_c == 0, "track %d: frames of 2, 3 and 4 steps are the same race: %u of %d steps differ (the first %u)", track, bad_c, NSTEPS, first_c);
    /* the old order: the same controls, the frames' player steps first. Its snapshots are per frame; the frames' ends are compared with the steps of the one-step race */
    static struct world d[NSTEPS];
    race_from_controls(track, manual0, late, 8, 0, d);
    uint32_t differ = 0, first = 0, ends = 0;
    for (uint32_t k = 0, f = 0; k < NSTEPS; f++) {
        k += (uint32_t)late[f % 8];
        if (k > NSTEPS) break;
        ends++;
        if (memcmp(&a[k - 1], &d[k - 1], sizeof a[0])) { if (!differ) first = k - 1; differ++; }
    }
    printf("  track %d: %d steps in frames of 1 .. 4 are the one-step race; the old order (a frame's player steps, then its computers') differs at %u of %u frame ends (the first at step %u)\n",
           track, NSTEPS, differ, ends, first);
}

/* ------------------------------------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    if (argc < 2) return 2;
    race_grid[0] = (struct s2_pick){ 1, 1 };
    race_grid[1] = (struct s2_pick){ 2, 2 };
    race_grid[2] = (struct s2_pick){ 3, 3 };
    race_grid[3] = (struct s2_pick){ 4, 4 };
    for (int track = 1; track <= 7; track++) {
        CHECK(image(argv[1], track), "track %d: the image", track);
        printf("track %d\n", track);
        batching(track);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
