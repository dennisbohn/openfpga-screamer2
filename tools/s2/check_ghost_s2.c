/*
 * Host test of time attack's ghost (firmware/s2ghost.c and the race kinds of firmware/race.c), run by tools/s2/check_ghost_s2.sh: the firmware's own code on the PC
 * (the physics, the race, the track image of tools/s2/make_track_s2.py), the SDRAM mapped at its addresses, the game's files for the car's numbers (S2_GAME).
 *
 *   unit        the controls as a byte for every possible byte, the head of a block, the CRC (every byte of a short block damaged in turn), another physics or
 *               setting, the rule of keeping (none kept, slower, equal, quicker), the seven blocks apart, a recording that is too long, a track that is not one
 *   set-up      the car's set-up (CUSTOMIZE, docs/screamer2/driving.md "The set-up") in the head: kept with the run, in range or the block is no run; a block of the first version
 *               (32 bytes of head, no set-up) is made over into the second where it is looked at (the controls, time, CRC kept; one too long to move or damaged is erased)
 *   record      a pilot (steering to a point of the road ahead, the gas by a speed it keeps) drives each track's first lap in a race of RACE_ATTACK (one lap, the
 *               kinds of race are the same code at three): the controls are recorded, the run is kept, then it is raced as the ghost beside a player who stands
 *               still: for every step the ghost's car equals the recorded car bit for bit (place, heading, speed, the car's whole record), and its lap time,
 *               best lap and the step in which it has done its laps are the recorded ones; the runs are made with the standard set-up and with set-ups of CUSTOMIZE
 *               (the ghost takes the run's, not the player's; a negative control: a ghost made with the wrong set-up is another car)
 *   batching    the same run with the steps of a frame taken two or three at a time (as the viewer does when a frame is late) is the same run
 *   random      random controls for 6,000 steps, also the gearbox switched and the car off the road for long enough to be put back on the line: the same
 *   sides       the player takes the side the kept run did not start on, and the next run kept (quicker) starts on the one the player had
 *   free run    a race of RACE_FREE does not end on its laps, its markers pass five a lap, the list of splits fills and clears as 0x4612d does
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
#include "race.h"
#include "s2ghost.h"
#include "save.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))

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

static uint32_t rng_state = 12345;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* ------------------------------------------------------------------------------------------------ unit */

static void unit(void)
{
    /* every byte: unpack, pack */
    for (int b = 0; b < 256; b++) {
        if ((b & 12) == 12) continue;                       /* (left and right together are not a control the buttons give: the d-pad cannot) */
        int manual;
        struct s2car_input in = s2gh_unpack((uint8_t)b, &manual);
        const int left = (b & 4) != 0, right = (b & 8) != 0;
        CHECK(manual == ((b & 128) != 0) && in.throttle == ((b & 1) ? 1.0f : 0.0f) && in.brake == ((b & 2) ? 1.0f : 0.0f) &&
              in.steer == (float)right - (float)left && in.handbrake == ((b & 16) != 0) && in.gear_up == ((b & 32) != 0) && in.gear_down == ((b & 64) != 0),
              "unpack of %d", b);
        CHECK(s2gh_pack(&in, manual) == (uint8_t)b, "pack of %d", b);
    }
    /* analog values are made digital by the half */
    struct s2car_input a = { .throttle = 0.6f, .brake = 0.4f, .steer = -0.7f };
    CHECK(s2gh_pack(&a, 0) == (1 | 4), "analog controls");

    memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
    for (int t = 1; t <= 7; t++) CHECK(s2gh_stored(t) == 0 && s2gh_ghost(t, 0) == 0, "an empty window has no run (track %d)", t);
    CHECK(s2gh_stored(0) == 0 && s2gh_stored(8) == 0 && s2gh_ghost(-1, 0) == 0, "tracks out of range");

    /* nothing recorded: nothing kept */
    s2gh_rec_begin(2, 3, 4, 1, 0, 0, 0);
    CHECK(s2gh_keep(80.0f, 26.0f, 3) == 0, "an empty recording is not kept");
    struct s2car_input in = { .throttle = 1.0f };
    for (int i = 0; i < 1000; i++) {
        in.steer = (float)(int)(rnd() % 3) - 1.0f;
        s2gh_rec_step(&in, (int)(rnd() & 1));
    }
    CHECK(s2gh_rec_steps() == 1000, "1000 steps recorded");
    CHECK(s2gh_keep(0.0f, 1.0f, 3) == 0 && s2gh_keep(-3.0f, 1.0f, 3) == 0 && s2gh_keep(4000.0f, 1.0f, 3) == 0 && s2gh_keep(NAN, 1.0f, 3) == 0, "a time that is not a time is not kept");
    CHECK(s2gh_stored(2) == 0, "nothing was written");
    CHECK(s2gh_keep(80.5f, 26.0f, 3) == 1, "the first run is kept");
    const struct s2gh_head *h = s2gh_stored(2);
    CHECK(h && h->steps == 1000 && h->track == 2 && h->team == 3 && h->car == 4 && h->slot == 1 && h->damages == 0 && h->time == 80.5f && h->best_lap == 26.0f &&
          h->laps == 3 && h->sim == S2GH_SIM, "the head of the kept run");
    CHECK(s2gh_ghost(2, 0) == h, "it is a ghost to race");
    CHECK(s2gh_ghost(2, 1) == 0 && s2gh_stored(2) == h, "another DAMAGES setting: not raced, still the time to beat");
    for (int t = 1; t <= 7; t++) if (t != 2) CHECK(s2gh_stored(t) == 0, "the other tracks are still empty (%d)", t);
    /* the controls come back */
    static uint8_t copy[S2GH_MAX];
    memcpy(copy, s2gh_data(h), 1000);
    s2gh_rec_begin(2, 3, 4, 0, 0, 0, 0);
    for (int i = 0; i < 1000; i++) {
        int manual;
        struct s2car_input u = s2gh_unpack(copy[i], &manual);
        s2gh_rec_step(&u, manual);
    }
    CHECK(s2gh_keep(80.5f, 25.0f, 3) == 0, "an equal time is not quicker");
    CHECK(s2gh_keep(81.0f, 25.0f, 3) == 0, "a slower time is not kept");
    CHECK(s2gh_stored(2) == h && h->slot == 1, "the kept run is untouched");
    CHECK(s2gh_keep(79.99f, 25.0f, 3) == 1, "a quicker time is kept");
    h = s2gh_stored(2);
    CHECK(h && h->slot == 0 && h->time == 79.99f && h->best_lap == 25.0f && !memcmp(s2gh_data(h), copy, 1000), "the quicker run replaces it, with its own head and controls");

    /* every byte of a short block damaged in turn: not whole (except those the CRC does not cover: the marker is checked by itself) */
    uint8_t *blk = (uint8_t *)(uintptr_t)(SAVE_ATTACK_ADDR + 1 * S2GH_BLOCK);       /* track 2's block */
    uint8_t saved[S2GH_HEAD + 1000];
    memcpy(saved, blk, sizeof saved);
    int undetected = 0;
    for (unsigned i = 0; i < sizeof saved; i++) {
        blk[i] ^= 0x10;
        if (s2gh_stored(2)) {
            undetected++;
            printf("note: damage of byte %u not detected\n", i);
        }
        blk[i] = saved[i];
    }
    CHECK(undetected == 0, "damage of a byte of a block is detected (%d not)", undetected);
    CHECK(s2gh_stored(2) != 0, "the block is whole again");
    /* the head's fields out of range with a matching CRC */
    struct s2gh_head *w = (struct s2gh_head *)blk;
    w->team = 5;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(2) == 0, "a team out of range is not a run");
    memcpy(blk, saved, sizeof saved);
    w->steps = S2GH_MAX + 1;
    w->crc = crc32(blk + 8, blk + 8 + 24);
    CHECK(s2gh_stored(2) == 0, "too many steps is not a run");
    memcpy(blk, saved, sizeof saved);
    w->sim = S2GH_SIM + 1;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(2) != 0 && s2gh_ghost(2, 0) == 0, "another physics: the time to beat, not a ghost");
    memcpy(blk, saved, sizeof saved);
    w->track = 3;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(2) == 0, "a block of another track is not this track's");
    memcpy(blk, saved, sizeof saved);

    /* a recording too long is not kept, one of the longest length is */
    s2gh_rec_begin(5, 1, 1, 0, 1, 0, 0);
    for (uint32_t i = 0; i < S2GH_MAX; i++) s2gh_rec_step(&in, 0);
    CHECK(s2gh_rec_steps() == S2GH_MAX, "the longest run");
    CHECK(s2gh_keep(900.0f, 300.0f, 3) == 1, "the longest run is kept");
    h = s2gh_stored(5);
    CHECK(h && h->steps == S2GH_MAX && h->damages == 1 && s2gh_ghost(5, 1) == h && s2gh_ghost(5, 0) == 0, "its head");
    s2gh_rec_begin(5, 1, 1, 0, 1, 0, 0);
    for (uint32_t i = 0; i < S2GH_MAX + 5; i++) s2gh_rec_step(&in, 0);
    CHECK(s2gh_rec_steps() == S2GH_MAX && s2gh_keep(100.0f, 30.0f, 3) == 0, "one step too many: not kept (the quicker time does not matter)");
    CHECK(s2gh_stored(5) == h && h->time == 900.0f, "the kept run is untouched");

    /* seven blocks fit and do not touch */
    for (int t = 1; t <= 7; t++) {
        s2gh_rec_begin(t, 1 + t % 4, 1 + t % 5, t & 1, 0, 0, 0);
        for (int i = 0; i < 300 + t; i++) {
            struct s2car_input u = { .throttle = (float)(t & 1), .steer = (float)(i % 3) - 1.0f };
            s2gh_rec_step(&u, t & 1);
        }
        s2gh_erase(t);
        CHECK(s2gh_keep(50.0f + (float)t, 20.0f, 3) == 1, "track %d kept", t);
    }
    for (int t = 1; t <= 7; t++) {
        const struct s2gh_head *g = s2gh_stored(t);
        CHECK(g && g->track == t && g->steps == (uint32_t)(300 + t) && g->time == 50.0f + (float)t, "track %d's block after the others", t);
    }
    s2gh_erase(4);
    CHECK(s2gh_stored(4) == 0 && s2gh_stored(3) && s2gh_stored(5), "one erased, the others stay");
    CHECK((uint32_t)S2GH_TRACKS * S2GH_BLOCK <= SAVE_ATTACK_SIZE, "the blocks are inside ATTACK.DAT's slot");
    memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
}

/* ------------------------------------------------------------------------------------------------ the set-up in the head */

static void unit_setup(void)
{
    memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
    struct s2car_input in = { .throttle = 1.0f };
    /* kept with the run */
    const struct s2car_setup su = { 3, 1, 2, 4, 0, 3 };
    s2gh_rec_begin(6, 2, 3, 1, 0, 1, &su);
    for (int i = 0; i < 500; i++) {
        in.steer = (float)(int)(rnd() % 3) - 1.0f;
        s2gh_rec_step(&in, 0);
    }
    CHECK(s2gh_keep(70.0f, 24.0f, 3) == 1, "a run with a set-up is kept");
    const struct s2gh_head *h = s2gh_stored(6);
    CHECK(h && h->version == 2 && h->custom == 1 && !memcmp(&h->setup, &su, sizeof su) && h->reserved == 0 && h->steps == 500 && s2gh_ghost(6, 0) == h, "the head has the set-up");
    s2gh_rec_begin(7, 1, 1, 0, 0, 0, &su);                  /* (a standard set-up has no numbers: those handed over are not kept) */
    for (int i = 0; i < 100; i++) s2gh_rec_step(&in, 0);
    CHECK(s2gh_keep(60.0f, 20.0f, 3) == 1, "a run with the standard set-up is kept");
    h = s2gh_stored(7);
    CHECK(h && h->custom == 0 && !h->setup.handling && !h->setup.pressure && !h->setup.tyres && !h->setup.front && !h->setup.rear && !h->setup.brakes, "its head has none");
    /* the numbers out of range, with a matching CRC, are no run */
    uint8_t *blk = (uint8_t *)(uintptr_t)(SAVE_ATTACK_ADDR + 5 * S2GH_BLOCK);       /* track 6's block */
    static uint8_t saved[S2GH_HEAD + 500];
    memcpy(saved, blk, sizeof saved);
    struct s2gh_head *w = (struct s2gh_head *)blk;
    static const struct { int field; int value; const char *what; } bad[] = {
        { 0, 5, "handling 5" }, { 0, -1, "handling -1" }, { 1, 5, "pressure 5" }, { 2, 3, "tyres 3" }, { 2, -2, "tyres -2" }, { 3, 5, "front 5" }, { 4, -1, "rear -1" }, { 5, 9, "brakes 9" },
    };
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        int8_t *f = &((int8_t *)&w->setup)[bad[k].field];
        *f = (int8_t)bad[k].value;
        w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
        CHECK(s2gh_stored(6) == 0, "a set-up with %s is no run", bad[k].what);
        memcpy(blk, saved, sizeof saved);
    }
    w->custom = 2;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(6) == 0, "custom 2 is no run");
    memcpy(blk, saved, sizeof saved);
    w->custom = 0;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(6) == 0, "a standard set-up with numbers is no run");
    memcpy(blk, saved, sizeof saved);
    w->setup.tyres = -1;                                    /* (the track's own tyres is a number the head may hold) */
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(6) != 0, "tyres -1 is a set-up");
    memcpy(blk, saved, sizeof saved);
    w->reserved = 1;
    w->crc = crc32(blk + 8, blk + S2GH_HEAD + w->steps);
    CHECK(s2gh_stored(6) != 0, "the reserved byte is not looked at");
    memcpy(blk, saved, sizeof saved);

    /* a block of the first version: the same head up to `laps` in 32 bytes, the controls from byte 32 */
    struct v1 { char magic[4]; uint32_t crc; uint8_t version, track, team, car, slot, damages; uint16_t sim; uint32_t steps; float time, best_lap; uint32_t laps; };
    _Static_assert(sizeof(struct v1) == S2GH_HEAD_V1, "the first version's head");
    for (int variant = 0; variant < 5; variant++) {
        const int track = 1 + variant;
        uint8_t *b = (uint8_t *)(uintptr_t)(SAVE_ATTACK_ADDR + (uint32_t)(track - 1) * S2GH_BLOCK);
        const uint32_t steps = variant == 3 ? S2GH_MAX + 1 : variant == 4 ? S2GH_MAX : 777;     /* 51,401 does not fit once the head is 8 bytes longer; 51,400 does */
        memset(b, 0, S2GH_BLOCK);
        struct v1 *o = (struct v1 *)b;
        memcpy(o->magic, "S2AT", 4);
        o->version = 1; o->track = (uint8_t)track; o->team = 2; o->car = 3; o->slot = 1; o->damages = 0; o->sim = S2GH_SIM; o->steps = steps;
        o->time = 91.25f; o->best_lap = 30.5f; o->laps = 3;
        for (uint32_t i = 0; i < steps; i++) b[S2GH_HEAD_V1 + i] = (uint8_t)(i * 7 + 3);
        o->crc = crc32(b + 8, b + S2GH_HEAD_V1 + steps);
        if (variant == 2) b[S2GH_HEAD_V1 + 100] ^= 1;                                          /* damaged */
        const struct s2gh_head *g = s2gh_stored(track);
        if (variant == 2 || variant == 3) {
            CHECK(g == 0, "a first-version block that is %s is no run", variant == 2 ? "damaged" : "too long to move");
            CHECK(s2gh_stored(track) == 0 && b[0] == 0, "... and it is erased");
            continue;
        }
        CHECK(g && g->version == 2 && g->track == (uint8_t)track && g->team == 2 && g->car == 3 && g->slot == 1 && g->damages == 0 && g->sim == S2GH_SIM && g->steps == steps &&
              g->time == 91.25f && g->best_lap == 30.5f && g->laps == 3 && g->custom == 0 && !g->setup.handling && !g->setup.brakes, "a first-version block is made over: head (variant %d)", variant);
        int same = g != 0;
        for (uint32_t i = 0; same && i < steps; i++) same = s2gh_data(g)[i] == (uint8_t)(i * 7 + 3);
        CHECK(same, "... the controls moved with it (%u steps)", steps);
        CHECK(g && s2gh_ghost(track, 0) == g && s2gh_stored(track) == g && g->crc == crc32((const uint8_t *)g + 8, (const uint8_t *)g + S2GH_HEAD + steps), "... it is a ghost to race, whole in its new version, and stays so");
    }
    memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
}

/* ------------------------------------------------------------------------------------------------ the pilot */

static float ang(float a)
{
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

struct pilot {
    float s;                                /* where on the road loop the car is (searched near the last place; below -1e8: not known yet) */
    float speed_limit;                      /* km/h the gas holds */
    float sign;                             /* the steering that turns the car towards a bigger yaw: +1 or -1 */
};

static struct s2car_input pilot_input(struct pilot *p, const struct s2car_state *c, int manual)
{
    float bx = 0, by = 0, bz = 0, yw = 0, best = 1e30f;
    float bs = p->s;
    const float len = road_length();
    if (p->s < -1e8f) {                                     /* the first time: the nearest place of the whole loop */
        for (float d = 0.0f; d < len; d += 150.0f) {
            road_at(d, &bx, &by, &bz, &yw);
            const float dx = bx - c->pos[0], dy = by - c->pos[1], q = dx * dx + dy * dy;
            if (q < best) { best = q; bs = d; }
        }
    } else {
        for (float d = -600.0f; d <= 3000.0f; d += 150.0f) {
            road_at(p->s + d, &bx, &by, &bz, &yw);
            const float dx = bx - c->pos[0], dy = by - c->pos[1], q = dx * dx + dy * dy;
            if (q < best) { best = q; bs = p->s + d; }
        }
    }
    p->s = bs;
    const float look = 2500.0f + c->speed_kmh * 25.0f;
    road_at(p->s + look, &bx, &by, &bz, &yw);
    const float want = atan2f(bx - c->pos[0], -(by - c->pos[1]));
    const float err = ang(want - c->yaw);
    struct s2car_input in = { 0 };
    in.steer = err > 0.04f ? p->sign : err < -0.04f ? -p->sign : 0.0f;
    in.throttle = c->speed_kmh < p->speed_limit ? 1.0f : 0.0f;
    in.brake = c->speed_kmh > p->speed_limit + 25.0f ? 1.0f : 0.0f;
    (void)manual;
    return in;
}

/* ------------------------------------------------------------------------------------------------ runs */

struct snap { uint32_t hash; float x, y, z, yaw, speed; };

static uint32_t hash_words(const struct s2car_state *c)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof c->rec / 4; i++) {
        /* the car's number (word 0), what a finished car is given for the cars behind it (word 1) and its place (0xc30) are the race's, not the car's */
        h ^= (i == 0 || i == 1 || i == 0xc30 / 4) ? 0u : c->rec[i];
        h *= 16777619u;
    }
    return h;
}

static void take(struct snap *s, const struct s2car_state *c)
{
    *s = (struct snap){ hash_words(c), c->pos[0], c->pos[1], c->pos[2], c->yaw, c->speed_kmh };
}

#ifndef PILOT_SIGN
#define PILOT_SIGN (-1.0f)
#endif
#define MAX_STEPS 40000
static struct snap rec_snap[MAX_STEPS], gh_snap[MAX_STEPS];
static struct { float time, best_lap; uint32_t finish_step; uint32_t steps; int respawns; } run_info;

enum { PILOT, RANDOM, STILL };
static int total_respawns;
static float g_floor_add = -3000.0f;               /* the floor of a race: the lowest point of the road plus this (a high one puts the car back on the line when it dips) */

/* the same, one step at a time with the snapshots; batch > 1 takes that many player steps, then as many of the others' (a late frame) */
static void run(int track, int how, int batch, int after, int max, int manual, float speed, struct snap *snaps, struct snap *gsnaps)
{
    race_kind = RACE_ATTACK;
    race_laps = 1;
    CHECK(race_init(track, road_floor() + g_floor_add), "race_init (track %d)", track);
    race_car(0)->manual = manual;
    struct pilot pl = { -1e9f, speed, PILOT_SIGN };
    uint32_t k = 0, finish = 0;
    memset(&run_info, 0, sizeof run_info);
    while (k < (uint32_t)max) {
        int n = batch;
        if ((uint32_t)n > (uint32_t)max - k) n = (int)((uint32_t)max - k);
        for (int b = 0; b < n; b++) {
            struct s2car_input in = { 0 };
            if (how == PILOT) in = pilot_input(&pl, race_car(0), manual);
            else if (how == RANDOM) {
                const uint32_t r = rnd();
                if (r % 40 == 0) race_car(0)->manual = !race_car(0)->manual;
                in = (struct s2car_input){ .throttle = (r >> 8) % 8 ? 1.0f : 0.0f, .brake = (r >> 12) % 16 == 0, .steer = (float)(int)((r >> 16) % 3) - 1.0f,
                                           .handbrake = (r >> 20) % 50 == 0, .gear_up = (r >> 4) % 30 == 0, .gear_down = (r >> 6) % 40 == 0 };
                if (k + (uint32_t)b > 2200 && k + (uint32_t)b < 2500) { in.throttle = 1.0f; in.steer = 1.0f; }
            }
            race_step_player(&in);
            if (getenv("PILOT_TRACE") && *getenv("PILOT_TRACE") && how == PILOT && (k + (uint32_t)b) % 250 == 0) {
                const struct s2car_state *c = race_car(0);
                printf("    step %u: pos %.0f %.0f yaw %.2f speed %.1f gear %d s %.0f in t%.0f b%.0f s%.0f\n", k + (uint32_t)b, (double)c->pos[0], (double)c->pos[1], (double)c->yaw,
                       (double)c->speed_kmh, c->gear, (double)pl.s, (double)in.throttle, (double)in.brake, (double)in.steer);
            }
            if (snaps) take(&snaps[k + (uint32_t)b], race_car(0));
        }
        race_step_others((uint32_t)n);
        if (gsnaps && race_cars() > 1 && n == 1) take(&gsnaps[k], race_car(1));          /* (a snapshot per step needs a step at a time) */
        k += (uint32_t)n;
        struct race_hud h;
        race_get_hud(&h);
        if (h.finished && !finish) finish = k;
        if (finish && k >= finish + (uint32_t)after) break;
    }
    struct race_hud h;
    race_get_hud(&h);
    run_info.time = h.time;
    run_info.best_lap = h.best_lap;
    run_info.finish_step = finish;
    run_info.steps = k;
    run_info.respawns = (int)race_respawns();
}

/* the batches make the snapshots of the ghost per step impossible (its steps happen after the player's of the batch): the comparison uses batch 1 */

static void record_and_replay(int track, int how, int manual, float speed, int max, int after, const struct s2car_setup *su)
{
    static struct snap a[MAX_STEPS], g[MAX_STEPS];
    memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
    race_ghost = 0;
    race_grid[0] = (struct s2_pick){ 1, 1 };
    race_custom = su != 0;                                  /* the player's set-up: CUSTOMIZE's `su`, or the standard one */
    race_setup = su ? *su : (struct s2car_setup){ 0 };
    rng_state = 777 + (uint32_t)track;
    run(track, how, 1, after, max, manual, speed, a, 0);
    const uint32_t steps = run_info.steps, finish = run_info.finish_step;
    const float time = run_info.time, best_lap = run_info.best_lap;
    const int side = race_slot();
    CHECK(side == 1 && race_cars() == 1, "track %d: no ghost: the player alone, on side 1 (%d)", track, side);
    printf("  track %d (%s%s): %u steps, finished at step %u, total %.2f s, best lap %.2f s, %d respawns\n", track, how == PILOT ? "pilot" : "random", su ? ", set-up" : "", steps,
           finish, time, best_lap, run_info.respawns);
    if (how == PILOT && g_floor_add < 0.0f) CHECK(finish > 0, "track %d: the pilot did not finish the lap in %d steps", track, max);
    total_respawns += run_info.respawns;
    if (finish) CHECK(s2gh_rec_steps() > 0 && s2gh_rec_steps() <= finish, "track %d: %u steps recorded up to the step %u in which the car had done its laps", track, s2gh_rec_steps(), finish);
    CHECK(s2gh_keep(finish ? time : 100.0f, best_lap, 1) == 1, "track %d: the first run is kept", track);      /* (a run that did not finish is kept to be compared all the same) */
    const struct s2gh_head *gh = s2gh_ghost(track, 0);
    CHECK(gh && gh->steps == s2gh_rec_steps() && gh->time == (finish ? time : 100.0f), "track %d: a ghost to race", track);
    if (!gh) return;
    CHECK(gh->custom == (su != 0) && (!su || !memcmp(&gh->setup, su, sizeof *su)), "track %d: the run's set-up is in its head", track);
    race_custom = 0;                                        /* the player who stands still has the standard set-up: the ghost's is the head's */

    /* raced again: the player stands still on the other side, the ghost is the recorded car */
    race_ghost = gh;
    race_grid[1] = (struct s2_pick){ gh->team, gh->car };
    run(track, STILL, 1, 0, (int)steps, 0, 0.0f, 0, g);
    CHECK(race_cars() == 2 && race_slot() == 0, "track %d: the ghost races, the player on the other side (%d)", track, race_slot());
    uint32_t bad = 0, first = 0;
    const uint32_t upto = finish ? finish : steps;
    for (uint32_t k = 0; k < upto; k++)                     /* (up to the step in which the car had done its laps: after it the driver takes the car, which looks at the cars about it) */
        if (memcmp(&a[k], &g[k], sizeof a[k])) {
            if (!bad) first = k;
            bad++;
        }
    CHECK(bad == 0, "track %d: the ghost is the recorded car: %u of %u steps differ (the first %u)", track, bad, upto, first);
    const struct s2car_state *gc = race_car(1);
    float gtime, glap;
    memcpy(&gtime, &gc->rec[0xc38 / 4], 4);
    memcpy(&glap, &gc->rec[0xc40 / 4], 4);
    if (finish) {
        CHECK(gtime == time && glap == best_lap, "track %d: the ghost's total %.5f and best lap %.5f are the recorded %.5f and %.5f", track, (double)gtime, (double)glap,
              (double)time, (double)best_lap);
        CHECK((gc->rec[0x4c / 4] & 3) == 3, "track %d: the ghost has finished", track);
    }
    printf("    the ghost equals the recorded car in all %u steps%s\n", upto, finish ? ", its total and best lap are the recorded ones" : "");
    if (su) {                                               /* a negative control: the same run raced as a ghost made with the standard set-up is another car */
        static uint8_t wrong[S2GH_BLOCK];
        memcpy(wrong, gh, S2GH_BLOCK);
        struct s2gh_head *wh = (struct s2gh_head *)wrong;
        wh->custom = 0;
        memset(&wh->setup, 0, sizeof wh->setup);
        race_ghost = wh;
        run(track, STILL, 1, 0, (int)steps, 0, 0.0f, 0, g);
        uint32_t differ = 0;
        for (uint32_t k = 0; k < upto; k++) differ += memcmp(&a[k], &g[k], sizeof a[k]) != 0;
        CHECK(differ > upto / 2, "track %d: a ghost made with the wrong set-up is another car (%u of %u steps differ)", track, differ, upto);
        printf("    negative control: a ghost made with the standard set-up differs from the recorded car in %u of %u steps\n", differ, upto);
    }
    race_custom = 0;
    race_ghost = 0;
    race_grid[1] = (struct s2_pick){ 2, 2 };
}

/* ------------------------------------------------------------------------------------------------ the free run */

static void free_run(int track)
{
    race_kind = RACE_FREE;
    race_ghost = 0;
    race_laps = 1;
    race_free_reset();
    CHECK(race_init(track, road_floor() - 3000.0f), "free run: race_init");
    CHECK(race_cars() == 1, "the free run's one car");
    struct pilot pl = { -1e9f, 120.0f, PILOT_SIGN };
    const struct race_free *fr = race_free_state();
    int markers_seen = 0, laps_done = 0, prev_lt = -1, filled_max = 0;
    float last_lap = 0.0f;
    uint32_t cleared = 0, held_steps = 0;
    for (uint32_t k = 0; k < 40000 && laps_done < 3; k++) {
        struct s2car_input in = pilot_input(&pl, race_car(0), 0);
        race_step_player(&in);
        race_step_others(1);
        struct race_hud h;
        race_get_hud(&h);
        CHECK(!h.finished, "free run: the race never ends on its laps");
        int filled = 0;
        for (int i = 0; i < 5; i++) filled += fr->lt[i] < 3599.0f;
        if (filled > filled_max) filled_max = filled;
        if (fr->list_left > 0.0f) held_steps++;
        if (fr->list_left < 0.0f && fr->lt[4] > 3599.0f && held_steps) { cleared++; held_steps = 0; }
        int lap = h.lap;
        (void)lap;
        const int cp = (int)(race_car(0)->rec[0x4b4 / 4]);
        if (cp != prev_lt) { prev_lt = cp; markers_seen++; }
        uint32_t laps_word = race_car(0)->rec[0x4a8 / 4];
        if ((int)laps_word > laps_done) {
            laps_done = (int)laps_word;
            float lt; memcpy(&lt, &race_car(0)->rec[0xc40 / 4], 4);
            last_lap = lt;
        }
    }
    CHECK(laps_done >= 3, "free run: three laps done without an end (%d)", laps_done);
    CHECK(markers_seen >= 15, "free run: the markers pass five a lap (%d seen)", markers_seen);
    CHECK(filled_max == 5, "free run: the left column fills (%d rows)", filled_max);
    CHECK(fr->best[4] < 3599.0f && fr->best[4] >= fr->best[0] && fr->best[3] >= fr->best[2], "free run: the quickest lap's splits rise A to E (%.2f %.2f %.2f %.2f %.2f)",
          (double)fr->best[0], (double)fr->best[1], (double)fr->best[2], (double)fr->best[3], (double)fr->best[4]);
    CHECK(fr->best_lap < 3599.0f && fr->best_lap <= last_lap + 0.001f, "free run: the session's best lap %.2f", (double)fr->best_lap);
    CHECK(fabsf(fr->best[4] - fr->best_lap) < 0.05f, "free run: the E split is the lap (%.3f against %.3f)", (double)fr->best[4], (double)fr->best_lap);
    CHECK(cleared >= 1, "free run: the list is held for 4 s after a lap, then cleared (%u)", cleared);
    printf("  free run on track %d: %d markers, best lap %.2f s, splits %.2f %.2f %.2f %.2f %.2f\n", track, markers_seen, (double)fr->best_lap, (double)fr->best[0],
           (double)fr->best[1], (double)fr->best[2], (double)fr->best[3], (double)fr->best[4]);
    race_free_reset();
    CHECK(fr->best_lap > 3599.0f && fr->best[0] > 3599.0f, "free run: reset forgets the best lap");
}

int main(int argc, char **argv)
{
    map();
    s2car_damages = 0;
    printf("unit tests\n");
    unit();
    unit_setup();
    printf("  %d checks, %d failures\n", checks, failures);
    if (argc < 2) return failures != 0;

    /* the races: <image dir> holds car1.bin .. car7.bin (tools/s2/make_track_s2.py, the five cars of team 1) */
    race_grid[0] = (struct s2_pick){ 1, 1 };
    for (int track = 1; track <= 7; track++) {
        char path[512];
        snprintf(path, sizeof path, "%s/car%d.bin", argv[1], track);
        FILE *f = fopen(path, "rb");
        if (!f) { perror(path); return 2; }
        size_t n = fread(TEX_FILE, 1, 0x700000, f);
        fclose(f);
        CHECK(n > 1000 && track_open(TEX_FILE) && track_init(), "track %d: the image", track);
        CHECK(road_build() > 0, "track %d: the road", track);
        printf("track %d\n", track);
        static const struct s2car_setup su_random = { 4, 0, 1, 1, 4, 0 }, su_pilot = { 0, 4, 2, 4, 0, 4 }, su_lap = { 3, 1, 0, 3, 1, 3 };
        record_and_replay(track, PILOT, 0, track == 7 || track == 5 ? 70.0f : 95.0f, 14000, 200, track % 2 ? 0 : &su_lap);        /* (the standard set-up on the odd tracks, a CUSTOMIZE one on the even) */
        record_and_replay(track, RANDOM, 1, 0.0f, 6000, 0, track % 2 ? &su_random : 0);
        g_floor_add = 120.0f;                               /* a high floor: the car is put back on the line each time it is lower than 120 above the road's lowest point */
        record_and_replay(track, PILOT, 1, 80.0f, 6000, 0, track % 2 ? 0 : &su_pilot);
        g_floor_add = -3000.0f;
        if (track == 1) {
            /* batching: the same run with frames of 2 and 3 steps (late frames) is the same run: only the player's snapshots can be compared */
            static struct snap a[MAX_STEPS], b[MAX_STEPS], c[MAX_STEPS];
            memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
            race_ghost = 0;
            rng_state = 99;
            run(track, RANDOM, 1, 0, 5000, 1, 0.0f, a, 0);
            rng_state = 99;
            run(track, RANDOM, 2, 0, 5000, 1, 0.0f, b, 0);
            rng_state = 99;
            run(track, RANDOM, 3, 0, 5000, 1, 0.0f, c, 0);
            uint32_t bad2 = 0, bad3 = 0;
            for (int k = 0; k < 5000; k++) {
                bad2 += memcmp(&a[k], &b[k], sizeof a[k]) != 0;
                bad3 += memcmp(&a[k], &c[k], sizeof a[k]) != 0;
            }
            CHECK(bad2 == 0 && bad3 == 0, "batching: the same run whatever the steps of a frame (%u, %u steps differ)", bad2, bad3);
            printf("  batching: 5,000 random steps in frames of 1, 2 and 3 steps agree\n");
        }
    }
    CHECK(total_respawns > 0, "the random runs were put back on the line at least once (%d times in all)", total_respawns);
    printf("sides\n");
    {
        /* a second run, quicker, is kept and starts on the side the player had; slower is not kept */
        const int track = 4;
        char path[512];
        snprintf(path, sizeof path, "%s/car%d.bin", argv[1], track);
        FILE *f = fopen(path, "rb");
        size_t n = f ? fread(TEX_FILE, 1, 0x700000, f) : 0;
        if (f) fclose(f);
        CHECK(n > 1000 && track_open(TEX_FILE) && track_init() && road_build() > 0, "image of track 4");
        memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
        race_ghost = 0;
        race_grid[0] = (struct s2_pick){ 1, 1 };
        run(track, PILOT, 1, 100, 12000, 0, 70.0f, 0, 0);
        const float t1 = run_info.time;
        CHECK(race_slot() == 1 && s2gh_keep(t1, run_info.best_lap, 1) == 1, "first run kept from side 1");
        race_ghost = s2gh_ghost(track, 0);
        race_grid[1] = (struct s2_pick){ 1, 1 };
        run(track, PILOT, 1, 100, 12000, 0, 110.0f, 0, 0);
        const float t2 = run_info.time;
        CHECK(race_slot() == 0 && t2 < t1, "against the ghost the player takes side 0, and is quicker (%.2f against %.2f)", (double)t2, (double)t1);
        CHECK(s2gh_keep(t2, run_info.best_lap, 1) == 1 && s2gh_stored(track)->slot == 0, "the quicker run is kept and started on side 0");
        race_ghost = s2gh_ghost(track, 0);
        run(track, PILOT, 1, 100, 12000, 0, 60.0f, 0, 0);
        CHECK(race_slot() == 1 && run_info.time > t2 && s2gh_keep(run_info.time, run_info.best_lap, 1) == 0, "a slower run against it is not kept; the player is back on side 1");
        race_ghost = 0;
    }
    printf("free run\n");
    {
        char path[512];
        snprintf(path, sizeof path, "%s/car1.bin", argv[1]);
        FILE *f = fopen(path, "rb");
        size_t n = f ? fread(TEX_FILE, 1, 0x700000, f) : 0;
        if (f) fclose(f);
        CHECK(n > 1000 && track_open(TEX_FILE) && track_init() && road_build() > 0, "image of track 1");
        free_run(1);
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
