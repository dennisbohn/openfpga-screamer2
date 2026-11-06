/*
 * The computers' set-ups (menus row 12, docs/screamer2/driving.md "The set-up"): the numbers firmware/race.c's cars are made of against the original's. The reference (OpenScreamer's
 * routines, tools/s2/ref_trace/ref_run.sh race with OS_S2GAME: the kind of game 1 the arcade, 2 the championship) runs a race of four cars and writes every car's whole record after
 * every frame; in the championship (the mode word without bit 1) 0x14e20 makes a computer's car of the tyres' best pressure, the springs 4 (softer by the track, the rear's 3 for a car
 * that is not four-wheel drive) and the most handling, in the arcade it leaves the standard set-up (2 2 -1 2 2 2, the rear-drive car's handling 1). Here the firmware's race is set up with
 * race_tuned 0 / 1 on the same grid and each car's record of frame 0 is compared with the reference's in the fields the set-up makes: the tyre set (+0x50), the grip (+0x74), the most
 * revs (+0x5c), the steering (+0x6c), the brakes (+0x9c), the springs' stiffness (+0x78 / +0x7c and their copies) and the wheels' springs (rest heights, travels, +0x2c).
 * A negative control: the arcade's cars against the championship's reference (and the reverse) must differ in some of the computers' fields.
 *
 * Usage: check_setup_ai <track image> <track 1 .. 7> <reference race, mode 1> <reference race, mode 2>; run by tools/s2/check_setup_ai_s2.sh. Exit status 1 if a check fails.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "race.c"                                       /* (its static state is read below) */
#include "hw.h"
#include "track.h"
#include "road.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))
#define REC         0xcc0

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

/* the fields a set-up makes (offsets in the car's record) and whether they are floats */
static const struct { int off; int is_float; const char *what; } F_[] = {
    { 0x50, 0, "tyre set" }, { 0x2c, 1, "springs' mean" }, { 0x5c, 1, "most revs" }, { 0x6c, 1, "steering" }, { 0x74, 1, "grip" }, { 0x78, 1, "front stiffness" },
    { 0x80, 1, "front stiffness (copy)" }, { 0x7c, 1, "rear stiffness" }, { 0x84, 1, "rear stiffness (copy)" }, { 0x9c, 1, "brakes" },
    { 0x58c, 1, "wheel 0 rest" }, { 0x594, 1, "wheel 0 travel" }, { 0x59c, 0, "wheel 0 height" }, { 0x5fc, 1, "wheel 1 rest" }, { 0x604, 1, "wheel 1 travel" }, { 0x60c, 0, "wheel 1 height" },
    { 0x66c, 1, "wheel 2 rest" }, { 0x674, 1, "wheel 2 travel" }, { 0x67c, 0, "wheel 2 height" }, { 0x6dc, 1, "wheel 3 rest" }, { 0x6e4, 1, "wheel 3 travel" }, { 0x6ec, 0, "wheel 3 height" },
};
#define NF (int)(sizeof F_ / sizeof F_[0])

/* how many of the fields of car i differ from the reference's frame 0 */
static int differ(const uint8_t *ref, int i, int verbose, int track)
{
    int d = 0;
    for (int k = 0; k < NF; k++) {
        const uint8_t *p = ref + i * REC + F_[k].off;
        int bad;
        if (F_[k].is_float) {
            float a, b;
            memcpy(&a, p, 4);
            b = F(&R.car[i], F_[k].off);
            bad = !(fabsf(a - b) <= 1e-6f * fmaxf(1.0f, fabsf(a)));
            if (bad && verbose) printf("  track %d car %d %s (+0x%x): reference %.9g, firmware %.9g\n", track, i, F_[k].what, F_[k].off, (double)a, (double)b);
        } else {
            int32_t a, b = I(&R.car[i], F_[k].off);
            memcpy(&a, p, 4);
            bad = a != b;
            if (bad && verbose) printf("  track %d car %d %s (+0x%x): reference %d, firmware %d\n", track, i, F_[k].what, F_[k].off, a, b);
        }
        d += bad;
    }
    return d;
}

static uint8_t buf[2][RACE_CARS * REC];

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: check_setup_ai <image> <track> <ref mode 1> <ref mode 2>\n"); return 2; }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    CHECK(n > 1000 && track_open(TEX_FILE) && track_init(), "the image");
    road_build();
    const int track = atoi(argv[2]);
    for (int m = 0; m < 2; m++) {
        FILE *rf = fopen(argv[3 + m], "rb");
        if (!rf) { perror(argv[3 + m]); return 1; }
        CHECK(fread(buf[m], 1, sizeof buf[m], rf) == sizeof buf[m], "the reference's frame 0 of mode %d", m + 1);
        fclose(rf);
    }
    /* the reference's grid is team 1 car 2, team 2 car 3, team 3 car 4, team 4 car 1 */
    static const int team[4] = { 1, 2, 3, 4 }, car[4] = { 2, 3, 4, 1 };
    for (int i = 0; i < RACE_CARS; i++) race_grid[i] = (struct s2_pick){ (uint8_t)team[i], (uint8_t)car[i] };
    race_level = 1;
    race_laps = 1;
    race_kind = RACE_NORMAL;
    race_custom = 0;
    int same[2] = { 0, 0 }, other[2] = { 0, 0 };
    for (int tuned = 0; tuned < 2; tuned++) {
        race_tuned = tuned;
        CHECK(race_init(track, road_floor() - 3000.0f), "track %d: race_init", track);
        for (int i = 0; i < RACE_CARS; i++) {
            const int d = differ(buf[tuned], i, 1, track);
            CHECK(d == 0, "track %d car %d: %d of %d fields differ from the %s reference", track, i, d, NF, tuned ? "championship's" : "arcade's");
            same[tuned] += d;
            if (i > 0) other[tuned] += differ(buf[!tuned], i, 0, track);                 /* the control: the other mode's reference */
        }
        CHECK(other[tuned] > 0, "track %d: the %s cars are no different from the %s reference's: the check cannot tell them apart", track, tuned ? "championship's" : "arcade's",
              tuned ? "arcade's" : "championship's");
        printf("  track %d, %s: the player's and the three computers' %d fields equal the reference's; against the other mode's reference the computers' fields differ in %d places\n", track,
               tuned ? "championship (computers tuned)" : "arcade (standard)", NF, other[tuned]);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
