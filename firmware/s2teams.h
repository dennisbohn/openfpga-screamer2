/*
 * The cars of a race: each grid slot has its own team's car (docs/screamer2/comparison.md, "Teams"). The game
 * loads a car per slot (STARTL.EXE 0x2f519: GFX/CARS<team>0<look>.DAT, AUTO<1 + model>), so a race of four needs
 * four cars and none of the teams' other sixteen. The track image holds those four as its car models (model k is
 * grid slot k, track_build_s2()); this module picks them from the ZIP and unpacks them.
 *
 * The grid is the selection: slot 0 is the player's, the others are the computers'. team 1..4 and car 1..5 as the
 * files number them (CARS<team>, AUTO<car>; the game's fifth car is a team's bonus car). The race owns it
 * (race_grid, race.h) and uses it to set the cars up; the viewer hands it to s2_cars_load() when the track is
 * loaded, so a menu sets it before that.
 */
#pragma once
#include <stdint.h>
#include "track_build.h"
#include "zip.h"

struct s2_pick { uint8_t team, car; };

#define S2_CAR_ROOM     0x38000u                    /* bytes for a car's unpacked .MLB and .3DX (the largest pair: 211,000) */
#define S2_CARS_ROOM    (S2_CARS * S2_CAR_ROOM)

/* The cars of the grid (S2_CARS picks), look 0 (day) or 1 (night), unpacked into blobs (S2_CARS_ROOM bytes,
   4-aligned): the .MLB and the .3DX of each, which track_build_s2() takes. Only the two entries of each car are
   read from the ZIP when it is stored (method 0): the container's directory and the entries, about 60 KB a car
   instead of 0.4 to 0.8 MB. A deflated container can only be read whole. stage (4-aligned, stage_max >= the
   largest container + 8, 1 MB) is the place for those bytes. The cars loaded stay: a call with the same grid and
   look reads nothing. bytes: the bytes read from the ZIP are added. Returns the cars (S2_CARS of them, static),
   or 0 and in *what the file or entry that failed. */
const struct s2_car *s2_cars_load(struct zip *z, const struct s2_pick *grid, int look, uint8_t *stage,
                                  uint32_t stage_max, uint8_t *blobs, uint32_t *bytes, const char **what);

/* The cars AUTO<car>.MLB and .3DX of a whole container (file, size bytes) unpacked into to (room bytes), the
   .3DX after the .MLB: 1, or 0 if an entry is missing, empty or does not fit. (The same unpacking s2_cars_load()
   does from the pieces; the native checks use it on the container as a file.) */
int s2_car_unpack(const uint8_t *file, uint32_t size, int car, uint8_t *to, uint32_t room, struct s2_car *c);
