/*
 * Host test of the attract race (menus row 9: firmware/race.c's race_attract, firmware/s2cam.c's rotation), run by tools/s2/check_attract_s2.sh: the firmware's own code on the
 * PC (the physics, the race, the track image of tools/s2/make_track_s2.py), the SDRAM mapped at its addresses, the game's files for the car's numbers (S2_GAME). It builds on
 * tools/s2/check_ghost_s2.c (the image files, the CHECK macro, the pilot), which it includes.
 *
 * On each of the seven tracks the attract race (four computer cars on the grid, nobody to drive car 0, 90 s: 4,500 steps) is run in frames of one, of one to four and of two to
 * four steps and every car's whole record is kept after every step:
 *
 *   one race     the three runs agree at every step in all four cars (the race is the same whatever the frame's steps)
 *   no player    all four cars are computers' (record flag, the grid as the arcade's: car 0 last), a computer's pace is the Pro row's middle value (MISC.DTR's SKILLS, read by
 *                the script, from step 1 on: 0x2dd26), nothing is handed in as the controls (garbage is: the race does not change), no car has finished or has done 99 laps
 *   cars move    every car has gone round the line a good way in 90 s and the line's places are a valid order of 1 .. 4
 *   not kept     the recorder (s2ghost.c) holds what an earlier race left in it, byte for byte; no replay is offered (race_replay_ok); nothing else of the player's is touched
 *   afterwards   a normal race run after the attract race, and one run before it, are the same race at every step: nothing of the attract race stays in the race's state
 *   cameras      the rotation (s2cam.h): with the attract race's cars, in view 1 from the first frame, the clock's ten seconds set the flag at the first step at or after each
 *                multiple of ten (9 of them in 90 s) and the car followed goes on to the next (round the four) at the first change of camera after it, with a camera chosen for the
 *                new car in the same frame, and not otherwise (the follow only ever changes by +1 at a camera change); a replay's cameras (no flag) never change the car
 *
 * Exit status 1 if any check fails.
 */
#define main check_ghost_main
#include "check_ghost_s2.c"
#undef main
#include "s2cam.h"

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

#define NSTEPS 4500
static struct world run_a[NSTEPS], run_b[NSTEPS], run_c[NSTEPS], run_d[NSTEPS];
static const int ONE[1] = { 1 }, LATE[8] = { 1, 1, 2, 1, 3, 1, 4, 1 }, MANY[3] = { 2, 3, 4 };

union word { int32_t i; uint32_t u; float f; };
#define RF(c, o) (((const union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o) (((const union word *)(c)->rec)[(o) >> 2].i)

static void setup(int attract)
{
    race_kind = RACE_NORMAL;
    race_ghost = 0;
    race_replay = 0;
    race_laps = attract ? 99 : 3;
    race_level = attract ? 0 : 1;
    race_attract = attract;
    for (int i = 0; i < RACE_CARS; i++) race_grid[i] = (struct s2_pick){ (uint8_t)(i + 1), (uint8_t)(i + 1) };
}

static float pace0;                                     /* the Pro row's middle value of SKILLS, from the script */
static float min_progress[8];

/* the attract race (attract = 1) or an arcade race with a pilot for the player, in frames of the plan's sizes; garbage = the controls handed in are noise (the attract race must not read them) */
static void run_race(int track, int attract, const int *plan, int nplan, uint32_t nsteps, int garbage, struct world *w, int checks_on)
{
    struct pilot pl = { -1e9f, 85.0f, PILOT_SIGN };
    setup(attract);
    race_car(0)->manual = 0;
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    rng_state = 777 + (uint32_t)track;
    uint32_t k = 0;
    int pace_ok = 1, flags_ok = 1;
    float prog[RACE_CARS] = { 0 };
    for (int f = 0; k < nsteps; f++) {
        int n = plan[f % nplan];
        if ((uint32_t)n > nsteps - k) n = (int)(nsteps - k);
        for (int b = 0; b < n; b++) {
            struct s2car_input in;
            if (attract && garbage) in = (struct s2car_input){ .throttle = (float)(rnd() & 1), .brake = (float)(rnd() & 1), .steer = (float)(int)(rnd() % 3) - 1.0f, .handbrake = (int)(rnd() & 1) };
            else in = pilot_input(&pl, race_car(0), 0);
            race_step_player(&in);
            race_step_others(1);
            world_take(&w[k + (uint32_t)b]);
            if (checks_on) {
                for (int i = 0; i < RACE_CARS; i++) {
                    const struct s2car_state *c = race_car(i);
                    if (attract) {
                        if (!c->ai) flags_ok = 0;
                        if (RI(c, 0x4c) & 3) flags_ok = 0;                          /* (not driven by the finish, not finished) */
                        if (RF(c, 0x48) != pace0) pace_ok = 0;
                    }
                    if (RF(c, 0x4bc) > prog[i]) prog[i] = RF(c, 0x4bc);       /* (the way along the line in units, 0x4bc) */
                }
            }
        }
        k += (uint32_t)n;
    }
    if (checks_on && attract) {
        CHECK(pace_ok, "track %d: every computer's pace is the Pro row's middle value %.2f at every step", track, pace0);
        CHECK(flags_ok, "track %d: all four cars computers', none driven or finished in 90 s", track);
        int place_mask = 0;
        for (int i = 0; i < RACE_CARS; i++) {
            const int pl_ = RI(race_car(i), 0xc30);
            if (pl_ >= 1 && pl_ <= RACE_CARS) place_mask |= 1 << (pl_ - 1);
            CHECK(RI(race_car(i), 0x4a8) < 99, "track %d: car %d has not done 99 laps", track, i);
        }
        CHECK(place_mask == 0xF, "track %d: the places are 1 .. 4 (mask %x)", track, place_mask);
        float least = 1e9f;
        for (int i = 0; i < RACE_CARS; i++) if (prog[i] < least) least = prog[i];
        min_progress[track] = least;
        CHECK(least > 20000.0f, "track %d: every car has driven a good way in 90 s (the least: %.0f units along the line)", track, least);
    }
}

static uint32_t first_diff(const struct world *a, const struct world *b, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++)
        if (memcmp(&a[k], &b[k], sizeof *a)) return k + 1;
    return 0;
}

static void check_track(const char *dir, int track)
{
    CHECK(image(dir, track), "track %d: the image", track);
    /* a recording from an arcade race of the player's, which the attract race must leave alone */
    setup(0);
    CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
    struct pilot pl = { -1e9f, 85.0f, PILOT_SIGN };
    for (int i = 0; i < 600; i++) {
        struct s2car_input in = pilot_input(&pl, race_car(0), 0);
        race_step_player(&in);
        race_step_others(1);
    }
    const uint32_t rec_steps = s2gh_rec_steps();
    static uint8_t rec_copy[1000];
    CHECK(rec_steps == 600, "track %d: the arcade race recorded 600 steps (%u)", track, rec_steps);
    memcpy(rec_copy, s2gh_rec_data(), 600);
    /* a normal race before the attract race: the reference of "afterwards" */
    run_race(track, 0, LATE, 8, 1500, 0, run_a, 0);
    memcpy(rec_copy, s2gh_rec_data(), 600);             /* (that race recorded too: the recording to be left alone is its) */
    const uint32_t rec_after_normal = s2gh_rec_steps();
    /* the attract race three ways */
    run_race(track, 1, ONE, 1, NSTEPS, 0, run_b, 1);
    CHECK(s2gh_rec_steps() == rec_after_normal && !memcmp(rec_copy, s2gh_rec_data(), 600), "track %d: the attract race left the recording as it was (%u steps)", track, s2gh_rec_steps());
    CHECK(!race_replay_ok(), "track %d: no replay is offered after the attract race", track);
    run_race(track, 1, LATE, 8, NSTEPS, 0, run_c, 0);
    run_race(track, 1, MANY, 3, NSTEPS, 1, run_d, 0);
    uint32_t d1 = first_diff(run_b, run_c, NSTEPS), d2 = first_diff(run_b, run_d, NSTEPS);
    CHECK(d1 == 0, "track %d: the attract race in frames of 1 to 4 steps equals frames of one step (first difference at step %u)", track, d1);
    CHECK(d2 == 0, "track %d: ... and in frames of 2 to 4 steps with garbage handed in as the controls (first difference at step %u)", track, d2);
    /* a normal race after the attract race is the one it was before */
    static struct world after[1500];
    run_race(track, 0, LATE, 8, 1500, 0, after, 0);
    uint32_t d3 = first_diff(run_a, after, 1500);
    CHECK(d3 == 0, "track %d: an arcade race after the attract race is the same race as one before it (first difference at step %u)", track, d3);
    printf("  track %d: attract race 4,500 steps in three framings agree, least progress %.0f units, %u respawns\n", track, min_progress[track], race_respawns());
}

/* ------------------------------------------------------------------------------------------------ the cameras' rotation */

static void cameras(const char *dir)
{
    for (int track = 1; track <= 7; track++) {
        CHECK(image(dir, track), "track %d: the image", track);
        setup(1);
        CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
        s2cam_begin(track, S2CAM_TRACKSIDE);
        CHECK(s2cam_rows() > 0, "track %d: the track has cameras", track);
        const struct s2car_state *cars[RACE_CARS];
        for (int i = 0; i < RACE_CARS; i++) cars[i] = race_car(i);
        int follow_was = s2cam_get_follow(), shot_was = -1, pending = 0, flagged_ticks = 0, rotations = 0, bad_change = 0, bad_cam = 0, change_without_flag = 0;
        int rot_after_tick_ok = 1;
        float tick_clock[16];
        int ticks = 0;
        struct track_cam cam;
        char dump_path[256];
        FILE *dump = 0;
        if (getenv("ATTRACT_DUMP")) {                           /* the race and the camera every 0.1 s, for tools/attract_compare.py (against the running original) */
            snprintf(dump_path, sizeof dump_path, "%s/attract_dump_t%d.csv", getenv("ATTRACT_DUMP"), track);
            dump = fopen(dump_path, "w");
        }
        for (int step = 0; step < 4400; step++) {                                   /* (88 s: the flag at the 90th second is not reached) */
            struct s2car_input none = { 0 };
            race_step_player(&none);
            race_step_others(1);
            const float clock = race_get_clock();
            /* the rotation's flag, as the viewer calls it each frame (a step a frame here) */
            const int before = s2cam_get_follow();
            s2cam_attract_tick(clock, 0.02f);
            if ((int)clock % 10 == 0 && (int)(clock - 0.02f) % 10 != 0) {
                pending = 1;
                flagged_ticks++;
                if (ticks < 16) tick_clock[ticks++] = clock;
            }
            s2cam_frame(cars, RACE_CARS, 0.02f, &cam);
            const int follow = s2cam_get_follow(), shot = s2cam_shot();
            if (follow != before) {
                rotations++;
                if (follow != (before + 1) % RACE_CARS) bad_change = 1;
                if (!pending) change_without_flag = 1;
                if (shot < 0) bad_cam = 1;                                   /* a camera is chosen for the new car in the same frame */
                if (shot == shot_was && shot_was >= 0) rot_after_tick_ok = rot_after_tick_ok;   /* (the new car may be at the same camera) */
                pending = 0;
            }
            shot_was = shot;
            follow_was = follow;
            if (dump && step % 5 == 4) {
                fprintf(dump, "%d,%.2f,%d,%d", step + 1, clock, follow, shot);
                for (int i = 0; i < RACE_CARS; i++) fprintf(dump, ",%.1f,%.1f,%d,%.2f,%.2f,%.1f,%d", RF(cars[i], 0x4d8), RF(cars[i], 0x4e0), RI(cars[i], 0x4dc), RF(cars[i], 0x500), RF(cars[i], 0x4b8),
                                                               RF(cars[i], 0x4bc), RI(cars[i], 0xc30));
                fprintf(dump, "\n");
            }
        }
        if (dump) fclose(dump);
        (void)follow_was;
        CHECK(flagged_ticks == 8, "track %d: the clock's multiples of ten (10 .. 80 s: 8 in 88 s) set the flag %d times", track, flagged_ticks);
        for (int k = 0; k < ticks; k++) CHECK(tick_clock[k] >= 10.0f * (float)(k + 1) - 0.001f && tick_clock[k] < 10.0f * (float)(k + 1) + 0.021f, "track %d: flag %d at clock %.3f", track, k, tick_clock[k]);
        CHECK(!bad_change, "track %d: the car followed only ever goes on to the next car", track);
        CHECK(!change_without_flag, "track %d: the car followed changes only after the flag", track);
        CHECK(!bad_cam, "track %d: a camera is chosen for the new car at once", track);
        CHECK(rotations >= 1 && rotations <= flagged_ticks, "track %d: %d rotations for %d flags (a flag waits for a change of camera, and two can wait for one)", track, rotations, flagged_ticks);
        CHECK(rot_after_tick_ok, "track %d: rotation", track);
        printf("  track %d: %d cameras, %d flags in 88 s, %d changes of the car followed\n", track, s2cam_rows(), flagged_ticks, rotations);
        /* a replay's camera: no flag, so the car followed never changes by itself */
        s2cam_begin(track, S2CAM_TRACKSIDE);
        int changes = 0;
        for (int step = 0; step < 3000; step++) {
            struct s2car_input none = { 0 };
            race_step_player(&none);
            race_step_others(1);
            const int before = s2cam_get_follow();
            s2cam_frame(cars, RACE_CARS, 0.02f, &cam);
            changes += s2cam_get_follow() != before;
        }
        CHECK(changes == 0, "track %d: without the flag the cameras never change the car followed (%d)", track, changes);
    }
}

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    if (argc < 2) return 2;
    pace0 = argc > 2 ? strtof(argv[2], 0) : 1.2f;
    for (int track = 1; track <= 7; track++) check_track(argv[1], track);
    printf("cameras\n");
    cameras(argv[1]);
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
