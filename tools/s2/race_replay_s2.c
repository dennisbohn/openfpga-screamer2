/*
 * The firmware's race from states of the reference: the four cars' records of frame k0 of a reference trace (the
 * game's own routines, OpenScreamer's OS_S2TRACEFILE: 0xCC0 bytes a car, every frame) are put into race.c's cars, and
 * the race is run on from there, every car's record written after every step in the same format. So the two
 * implementations start from the very same state (before a contact) and the difference afterwards is theirs.
 * Built and run by tools/s2/check_aicontact_s2.sh with the firmware's own code natively in Docker; not part of the firmware.
 *
 * Usage: race_replay <track image> <track 1..7> <ref.bin> <steps> <out prefix> <k0>... [-g team:car,...]
 *   k0     the reference's frame whose state the race starts from (frame j of the file is the state after step j + 1); one run for each,
 *          written to <out prefix><k0>.bin, whose row j is the state after the step that the reference's frame k0 + 1 + j shows
 *   steps  how many steps to run from there
 * The reference places are whole numbers (words 0x4d8, 0x4e0 and their copies 0x734, 0x73c); the firmware's are floats and
 * are made from them. Build the firmware with -DS2_WHOLE_PLACES=1 to follow the reference to the unit.
 * REPLAY_GAS=1: the player holds the gas (the reference's keys "A"), else his car stands. Needs the game's files for the physics: S2_GAME=<game folder>.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "race.c"                                       /* (its static state is set below) */
#include "hw.h"
#include "track.h"
#include "road.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))      /* as the viewer */
#define REC         0xcc0

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

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: race_replay <image> <track> <ref.bin> <steps> <out prefix> <k0>... [-g team:car,...]\n");
        return 2;
    }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    if (!track_open(TEX_FILE)) { fprintf(stderr, "not a track image (%zu bytes)\n", n); return 1; }
    if (!track_init()) { fprintf(stderr, "track_init: the image does not fit the limits\n"); return 1; }
    road_build();
    const int track = atoi(argv[2]), steps = atoi(argv[4]);
    const char *grid = "1:2,2:3,3:4,4:1";
    int last = argc;
    for (int a = 6; a < argc; a++) if (!strcmp(argv[a], "-g") && a + 1 < argc) { grid = argv[a + 1]; last = a; break; }
    for (int i = 0; i < RACE_CARS && *grid; i++) {
        int t, c, used;
        if (sscanf(grid, "%d:%d%n", &t, &c, &used) < 2) break;
        race_grid[i].team = (uint8_t)t;
        race_grid[i].car = (uint8_t)c;
        grid += used;
        if (*grid == ',') grid++;
    }
    race_level = getenv("RACE_LEVEL") ? atoi(getenv("RACE_LEVEL")) : 1;
    FILE *rf = fopen(argv[3], "rb");
    if (!rf) { perror(argv[3]); return 1; }
    for (int a = 6; a < last; a++) {
        const int k0 = atoi(argv[a]);
        if (!race_init(track, road_floor() - 3000.0f)) { fprintf(stderr, "race_init failed\n"); return 1; }
        /* the reference's state of frame k0 */
        static uint8_t buf[RACE_CARS * REC];
        if (fseek(rf, (long)k0 * (long)sizeof buf, SEEK_SET) || fread(buf, 1, sizeof buf, rf) != sizeof buf) { fprintf(stderr, "no frame %d in %s\n", k0, argv[3]); return 1; }
        for (int i = 0; i < RACE_CARS; i++) {
            struct s2car_state *c = &R.car[i];
            memcpy(c->rec, buf + i * REC, REC);
            static const int whole[4] = { 0x4d8, 0x4e0, 0x734, 0x73c };     /* the game keeps these as whole numbers, the firmware as floats */
            for (int k = 0; k < 4; k++) F(c, whole[k]) = (float)I(c, whole[k]);
            c->hold = 0;
        }
        R.clock = 0.0f;
        for (int k = 0; k <= k0; k++) R.clock += DT;    /* (the clock after frame k0 + 1 steps, rounded to a float at every step as the game does) */
        R.steps = (uint32_t)k0 + 1;
        R.running = 1;
        for (int i = 0; i < R.n; i++) R.off[i] = R.still[i] = 0;
        char name[1024];
        snprintf(name, sizeof name, "%s%d.bin", argv[5], k0);
        FILE *out = fopen(name, "wb");
        if (!out) { perror(name); return 1; }
        for (int k = 0; k < steps; k++) {
            struct s2car_input in = { .throttle = getenv("REPLAY_GAS") != 0 };
            race_step(&in);
            for (int i = 0; i < RACE_CARS; i++) fwrite(race_car(i)->rec, 1, REC, out);
        }
        fclose(out);
    }
    fclose(rf);
    return 0;
}
