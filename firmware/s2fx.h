/*
 * Wheel smoke and shadows of Screamer 2's race (docs/development.md "Wheel smoke and shadows"; STARTL.EXE 0x204ae the smoke's spawn, 0x2073b the puffs each
 * frame, 0x1f4a7 the shadow, all read from the program). The objects are the image's (track_fx(): the meshes ASMOKE, A2SMOKE and ASHADOW of the track's
 * container with their materials in MISC.MLB, routine 224 after the image builder, smg.h); what these draw is seen through its see-through table.
 *
 * The smoke is the game's at DETAIL HIGH (the core draws its H sets only: DETAIL LEVEL keeps its value and nothing else). Every car has eight records of each
 * of the two kinds; every 0.08 s (the timer adds the step and spawns once it is over 0.05) while the car is on the ground, a puff at each rear corner of the
 * body when the tyres slide (slip under 0.93) or the road kind's flag 4 is set and the car is over speed 20: by the kind's flags & 3 one of 'asmoke' (1) or
 * 'a2smoke' (2), the first free record, the place the corner's ground, moved sideways and back by ((2 d & T) - d) x (x & 7) and x (z & 7) (d the kind's
 * dispersion, T the 100 Hz counter, x and z the car's place cut to whole numbers). A puff lasts 2/3 s and stays where it was spawned, except the camera's car's
 * ("carried"): its puffs stay at the same place in the car's frame. It faces the screen, its texture frame six times its age.
 *
 * The shadow is a quad on the ground, 30 units further from the camera than the car, 2 under the ground, with the car's heading and the ground's slope.
 *
 * Nothing here reaches back into the race: the car records are only read.
 */
#pragma once
#include "track.h"
#include "s2car.h"

#define S2FX_CARS   4                       /* the race's cars (race.h RACE_CARS) */
#define S2FX_PUFFS  8                       /* records of a kind a car has */
#define S2FX_MAX    (S2FX_CARS * (1 + 2 * S2FX_PUFFS))      /* objects: a shadow and every puff of every car */

/* A new race (or a replay, a restart): no puffs, the timers at 0 */
void s2fx_reset(void);

/* After the cars' steps of a frame: `steps` steps of 20 ms since the last call, each spawning and ageing the puffs as the game does every frame; clock the race's
   hundredths (the game's T, which the dispersion goes by), followed the car the camera chases (its puffs are carried), -1 for none */
void s2fx_update(struct s2car_state *const *cars, int n, int steps, uint32_t clock, int followed);

/* The shadow of car `car` (-1: none) is left out of s2fx_objects() from now on: the views from inside the car take the car and its shadow off the draw list (STARTL.EXE 0x2b6b4) */
void s2fx_hide_shadow(int car);

/* The shadows and the puffs for track_draw_cam(), at most max objects, seen from cam; returns how many */
int s2fx_objects(const struct track_cam *cam, struct s2car_state *const *cars, int n, struct track_object *objs, int max);

/* For the tests (tools/s2/check_fx_s2.sh): the puffs alive; the state of record j of kind k (0 asmoke, 1 a2smoke) of car i: 1 if it is alive, its place as the game keeps
   it (x, up, z: in the world, or in the car's frame when carried), its age (0 .. 1) and whether it is carried; loading a record's state (to follow the game where a
   frame of it is not a step of ours); the car's spawn timer (set it if `set`) */
int s2fx_puffs(void);
int s2fx_puff_state(int i, int k, int j, float p[3], float *age, int *carried);
void s2fx_puff_load(int i, int k, int j, int on, float age, const float p[3], int carried);
float s2fx_timer(int i, int set, float t);
