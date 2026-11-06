/*
 * Time attack's ghost (docs/screamer2/menus.md, row 7). The original keeps, per track, the best run of the player's controls (0x4a59a writes 0x20 bytes a
 * frame, 0x4a488 reads them) and races it again as a second car "driven" by those controls (0x2c132); nothing collides with it. The core does the same
 * and needs less: the physics steps a fixed 20 ms (docs/screamer2/driving.md), so the run is re-simulated from the controls, deterministically, and
 * a step's controls fit one byte (the Pocket's buttons are on or off: the gas, the brake, the steering to either side, the handbrake, a gear up or down,
 * the gearbox in use). A run of three laps is 15,000 steps = 15 KB; the kept run of a track is a block of S2GH_BLOCK bytes of ATTACK.DAT (nonvolatile
 * data slot 24, core/data.json unchanged: its 360,096 bytes hold seven blocks), the Pocket writes the slot back when the core is quit.
 *
 * A block: 40 bytes of head, then the controls, one byte for each step from the first (the grid's hold included) up to the step in which the
 * player's car had done its laps (the car is the game's driver's after that, the replay does the same by itself).
 *
 *    0  "S2AT"
 *    4  CRC-32 of bytes 8 .. 40 + steps
 *    8  version (2), track 1 .. 7, team 1 .. 4, car 1 .. 5 of the recorded car, slot 0 / 1 (the side it started on), damages (the GAME SETTINGS row)
 *   14  sim: the version of the physics the run was made with (S2GH_SIM; a run of another one is not raced: it would not end where it did)
 *   16  steps       20  total time (float, seconds)       24  best lap (float)       28  laps
 *   32  the car's set-up (a run is a function of the controls AND the set-up the car was made of, docs/screamer2/driving.md "The set-up"): 0 the standard one, 1 the six numbers that
 *       follow (s2car.h: handling, pressure, tyres, front, rear, brakes); 33 .. 38 the six, 39 0
 *
 * A block of version 1 (32 bytes of head, no set-up: the standard one, steps from byte 32) is made over into version 2 when it is first looked at (s2gh_stored): the
 * controls move 8 bytes, the CRC-32 is made again; a run too long to move is no run (it was 51,401 steps or more: 17 minutes).

 * The block is a ghost to race if it is whole (marker, version, CRC-32), made with this physics and the same DAMAGES setting; a block that is whole
 * but of another physics or setting still counts as the time to beat. Nothing here touches the hardware, so it is tested on the PC
 * (tools/s2/check_ghost_s2.sh).
 */
#pragma once
#include <stdint.h>
#include "s2car.h"

#define S2GH_BLOCK      51440u                  /* a track's block: 7 x 51,440 = 360,080 of the slot's 360,096 bytes */
#define S2GH_HEAD       40u
#define S2GH_HEAD_V1    32u                     /* the head of the first version (no set-up), made over where it is found */
#define S2GH_MAX        (S2GH_BLOCK - S2GH_HEAD)    /* steps in a kept run: 51,400 = 17 minutes */
#define S2GH_TRACKS     7
#define S2GH_SIM        1

struct s2gh_head {
    char magic[4];
    uint32_t crc;
    uint8_t version, track, team, car, slot, damages;
    uint16_t sim;
    uint32_t steps;
    float time, best_lap;
    uint32_t laps;
    uint8_t custom;                             /* the recorded car's set-up: 0 the standard one, 1 `setup` */
    struct s2car_setup setup;
    uint8_t reserved;
};

/* The controls of one step as a byte: bit 0 gas, 1 brake, 2 steering left, 3 steering right, 4 handbrake, 5 gear up, 6 gear down, 7 the gearbox is manual.
   The Pocket's controls are digital (the viewer's car_input()), so nothing is lost; steer, throttle and brake are -1 / 0 / 1 and 0 / 1. */
uint8_t s2gh_pack(const struct s2car_input *in, int manual);
struct s2car_input s2gh_unpack(uint8_t b, int *manual);

/* Track n's kept block (1 .. 7, in ATTACK.DAT's window) if it is a whole one, else 0. */
const struct s2gh_head *s2gh_stored(int track);
/* ... and a ghost to race: whole, made with this physics, the same DAMAGES setting (damages 0 / 1) */
const struct s2gh_head *s2gh_ghost(int track, int damages);
/* The controls of a block, one byte a step */
const uint8_t *s2gh_data(const struct s2gh_head *h);

/* The recording of a race: begun when the race is (race_init), a byte for each of the player's steps; a race longer than S2GH_MAX steps is not kept. It is time
   attack's ghost when the run is kept (s2gh_keep) and the arcade's and the championship's replay for as long as no other race has begun. */
void s2gh_rec_begin(int track, int team, int car, int slot, int damages, int custom, const struct s2car_setup *setup);
void s2gh_rec_step(const struct s2car_input *in, int manual);
uint32_t s2gh_rec_steps(void);
/* ... and read back: the arcade's and the championship's replay (race.c, menus row 10) runs the race again from the recording of the race just run. */
const uint8_t *s2gh_rec_data(void);
int s2gh_rec_full(void);                    /* the race ran over S2GH_MAX steps: only the first are there, the recording is not a race */

/* The player's car has done its laps in `time` seconds (best_lap the quickest of them, laps the race's): the recording is the track's kept run if the track
   has none that is whole or the time is quicker than the kept one's (an equal time is not). 1 if it was kept (written into ATTACK.DAT's window), else 0. */
int s2gh_keep(float time, float best_lap, int laps);

/* A block empty (the tests) */
void s2gh_erase(int track);
