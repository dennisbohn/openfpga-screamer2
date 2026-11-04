/*
 * The race of the viewer's driving mode: the player and three computer cars on the game's grid, a countdown,
 * the computers driven by the game's driver along the track's line (MAPnOPP.SPL), laps counted on that line,
 * places, the end of the race (docs/screamer2/driving.md, "The race"). Ported from OpenScreamer's hand-written
 * text of the game's routines (docs/NOTES2.md S2-5c, S2-5n, S2-5r): the line's curve (0x1099a, 0x2d59c, 0x108f5,
 * 0x4a831), where a car is along it (0x153c6), the driver (0x17726), places and pace (0x2da06), a lap done
 * (0x201ee). Single precision. The physics of every car is s2car.c: the same model for all four.
 */
#pragma once
#include <stdint.h>
#include "s2car.h"
#include "s2teams.h"

#define RACE_CARS   4
_Static_assert(RACE_CARS <= S2_CARS, "the track image holds one car model per grid slot");
#define RACE_LAPS   3                   /* the arcade race's default (the options' 1, 3, 5, 10) */

/* what the screen shows of the race */
struct race_hud {
    int count;                          /* the start lights: 3, 2, 1, then 0 (GO) for a second, -1 after that */
    int lap, laps;                      /* the player's lap, 1 .. laps */
    int place, cars;                    /* the player's place, 1 .. cars */
    int finished;                       /* the player has done his laps */
    float time, lap_time, best_lap;     /* seconds since the start; best_lap < 0: no lap done yet */
};

/* Who is on the grid: team 1..4 and car 1..5 as the files number them, slot 0 the player's (s2teams.h). Default team 1 car 1
   (the player), team 2 car 2, team 3 car 3, team 4 car 4. The track image holds these four cars, one model per slot in
   this order (the viewer loads them with s2_cars_load()), so a menu sets it before the track is loaded; race_init()
   takes the cars' physics and numbers from it. */
extern struct s2_pick race_grid[RACE_CARS];

/* The laps of a race (RACE_LAPS unless a menu says otherwise; the game's options are 1, 3, 5, 10) and the computers' level, the
   row of MISC.DTR's SKILLS: 0 pro, 1 normal (default), 2 rookie (the game's level = 2 - ARCADE ENEMIES SKILL). Set before
   race_init(). */
extern int race_laps;
extern int race_level;

/* Track 1 .. 7: the line read (once per track) and the four cars put on the grid, the player's car 0 last, the
   countdown at 0. 1 if it worked. floor_z: a car lower than this is put back on the line. */
int race_init(int track, float floor_z);

/* One 20 ms step of the race; in drives the player's car (until he has finished). */
void race_step(const struct s2car_input *in);

/* The same step in two halves, for the viewer: race_step_player() (the clock, places, the lights, the player's car) before the
   scene is drawn, race_step_others(n) (the computers' cars, cars touching, cars put back on the line) for n such steps after
   the pieces are handed to the hardware, while the rasterizer finishes the picture: it costs the frame little where the
   rasterizer is the bound. The computers' cars are then drawn one step old. */
void race_step_player(const struct s2car_input *in);
void race_step_others(uint32_t count);
/* The same in slices: begin(n), then slice() runs the next piece of work (one computer's step, or the cars touching and the
   rest of a step) and tells whether more is left; the viewer runs one while the geometry unit's buffer is full. */
void race_others_begin(uint32_t count);
int race_others_slice(void);

/* The hardest touch of the player's car with another since the last call (the speed the two close at, in the physics' units a
   second), 0 if none: the sound's knock (s2snd.h). */
float race_knock_take(void);

/* The cars: 0 is the player's. The state stays where it is until the next race_step / race_init. */
struct s2car_state *race_car(int i);
int race_cars(void);

void race_get_hud(struct race_hud *h);

/* Steps run so far (a step is 20 ms) */
uint32_t race_steps(void);

/* How many times a car has been put back on the line since race_init (a test counter, also in the log) */
uint32_t race_respawns(void);

/* The length of the line in units (a lap), and its nodes (the race screen's checkpoints are the fifths of a lap) */
int race_line_length(void);
int race_line_nodes(void);
