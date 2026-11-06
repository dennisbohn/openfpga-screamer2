/*
 * The obstacles of Screamer 2's tracks and the sparks of its races (docs/development.md "Obstacles and sparks"; STARTL.EXE 0x30a4e and 0x30b29 set the obstacles up, 0x2a639 lets
 * a car knock them, 0x2c475 flies them, 0x2aeb9 throws the sparks, 0x2cc70 flies them, all read from the program and held against the running game's memory:
 * tools/s2/check_props_s2.sh).
 *
 * OSTACLES of a track's MAPnDAT.DTR names the kinds (rows of three names: the cone AZCONE on England, California and Switzerland, the fence AZFENCE on California, the chest and
 * the crate AZCHEST and AZCRATE on Egypt; none on tracks 2, 5 and 7) and MAPnOST.SPL places them (23, 48, 53 and 22 of them; a node's x, z and row). An obstacle stands on the
 * road's ground (the height there, turned to the road's heading and leant to the ground's slope). A car whose middle is within 200 units of its height and 2,000 of its place and
 * whose nose, swept through the step, passes within the car's reach knocks it: it flies off along the car's way, up by the car's climb, at 155 x the car's speed
 * (the "units a second" of the game's speed number) and tumbles, falling at 7,848 a second squared, bouncing off the ground (0.7 of its speed) and off a wall (half), until its
 * speed is under 300, when it lies where it stopped. The knock changes nothing of the car (the game counts the cones a car hit in its record +0xc88 and nothing reads the count).
 * Every car of a race knocks them, in time attack only the player's.
 *
 * Sparks: where two cars touch (race.c's solver, 0x245a3) and where a car's knock hits a wall (s2car.c, 0x29571) the game throws speed x 0.5 of them (30 at most) from the place
 * 100 units up, two seconds to live, flung with chance and the knock's direction, each a pair of small triangles (AZTRI for the cars' set of thirty, AZ1TRI for the walls'; flat
 * colours of MISC.MLB). A touch starts at the set's first record and overwrites what is still flying there. The game's chance is its C library's rand seeded from its 100 Hz counter
 * at every call; here the counter is two a step.
 *
 * Nothing here reaches back into the race but the cones a car counts (+0xc88): the race is the player's controls' function whatever the props do. A new race (s2props_begin) stands
 * the obstacles up and clears the sparks; s2props_step() is the end of a step of the race (race.c calls it through race_step_hook), s2props_objects() the frame's objects for
 * track_draw_cam(), their blocks made when the drawing comes to them.
 */
#pragma once
#include <stdint.h>
#include "track.h"
#include "s2car.h"

#define S2PROPS_OBSTACLES   64          /* the most a track has is 53 (California); the game's table has room for 100 */
#define S2PROPS_SPARKS      60          /* thirty for the touching cars (set 1), thirty for the walls (set 0) */
#define S2PROPS_MAX         (S2PROPS_OBSTACLES + S2PROPS_SPARKS)    /* objects a frame can need */

/* Track `track`'s obstacles (gfx = GFX/MAP<n>H01.DAT, data = DATA/MAP<n>.DAT, as the viewer read them whole; misc_delta as s2anim_load; scratch, cap bytes, 4-aligned, at least
   24 KB) and the sparks' meshes. The obstacles stand as of the next s2props_begin(). Returns the obstacles (0: none, or the data is not there). */
int s2props_load(int track, const uint8_t *gfx, uint32_t gfx_size, const uint8_t *data, uint32_t data_size, int32_t misc_delta, uint8_t *scratch, uint32_t cap);

/* A race begins (the cars set up, race_init()): the obstacles stood up on the ground, no sparks flying, the hook of the sparks (s2car_sparks) set */
void s2props_begin(void);

/* The end of a step of the race (0x2b9b1 after the race's frame): the obstacles that are flying fly, the sparks fly, and the cars knock the obstacles. cars: the n cars of the
   race, car 0 the player's; attack: time attack or the free run, where only the player's car knocks them */
void s2props_step(struct s2car_state *cars, int n, int attack);

/* A touch's or a wall knock's sparks (0x2aeb9): at the place (x, height, z), thrown along dir (x and z), the knock's speed in units a second; set 1 the touching cars' thirty, 0 the walls' */
void s2props_spark(const float at[3], const float dir[3], float hit, int set);

/* The obstacles and the sparks flying, at most max objects, seen from cam; returns how many */
int s2props_objects(const struct track_cam *cam, struct track_object *objs, int max);

/* 1 if the player's car (car 0) knocked an obstacle since the last call (the sound of it: 0xaea54 with 0x10) */
int s2props_hit_take(void);

/* For the tests (tools/s2/check_props_s2.sh): the obstacles there are, and the state of obstacle i or, with `spark`, of spark i as the game keeps it in its record of 0x3c bytes:
   the place (x, height, z), the angles (+0xc, +0x10, +0x14), the speeds (x, up, z), a spark's seconds left, whether it is flying (a spark: alive) and whether the obstacle has
   been hit; setting it (the game's state put in, to follow it where a frame of it is not a step of ours) */
struct s2props_rec {
    float p[3], a[3], v[3], life;
    int on, hit;
};
int s2props_obstacles(void);
void s2props_get(int spark, int i, struct s2props_rec *r);
void s2props_set(int spark, int i, const struct s2props_rec *r);

/* For the tests: the step and the raw frame time the game worked with in a frame (0x133350, 0x133354; the firmware's are both 20 ms, the original's step is 1.1 x the time of its
   last pictures) */
void s2props_time(float step, float raw);
