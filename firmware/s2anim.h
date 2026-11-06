/*
 * The tracks' animated scenery (docs/development.md "Animated scenery"): birds, helicopters, an airship, cable cars, bulldozers and trains move along paths, and crowds, a
 * market, traffic, works, wolves and cows sound at fixed places. The rules are the original's (STARTL.EXE 0x30ec2 reads the paths, 0x31060 sets the rows up, 0x2cee4 runs them
 * every frame of its race, and only at DETAIL HIGH, which the core always has), written again in tools/s2/anim_ref.py and checked against it and against the running original
 * (tools/s2/check_anim_s2.sh, tools/s2/dosbox_anim.py).
 *
 * A track has up to 40 rows (DATA/MAPn.DAT: MAPnDAT.DTR's ANIMATIONS) and up to 40 paths (MAPn.PTH: three channels, x, y and height, of cubic keys). A row names a mesh
 * (GFX/MAPnH01.DAT's <name>.3DX; a name with an @ in front has none: a sound at its path's first place), the path, the seconds a run takes, the race clock at which it
 * starts, the lap it belongs to (the player's lap + 1; 0 any), the sample of the track's AMBIENT<n - 1>.DTR (SOUND.DAT) and flags (1: it starts when the player comes
 * within its range, 2: once only, 4: it faces the screen). A row is alive when the player is within 52,000 units of it (both ways) or, an object with neither of the first
 * two flags, always; its place on the path is the share of its run the race clock has reached, round and round.
 */
#pragma once
#include <stdint.h>
#include "track.h"

#define S2ANIM_ROWS     40              /* the original's limits: rows, paths, keys a channel, kinds of mesh */
#define S2ANIM_PATHS    40
#define S2ANIM_KEYS     24
#define S2ANIM_POOL     512             /* keys of all the paths of a set (the most any track has is 240) */
#define S2ANIM_MESHES   8
#define S2ANIM_OBJECTS  16              /* objects drawn at once (the most any track has alive in a frame is 6) */
#define S2ANIM_RANGE    80000.0f        /* how far a row's sound carries (0x2d525) */
#define S2ANIM_SAMPLE_BASE 0x40         /* the numbers of AMBIENT<n>.DTR's samples */

/* Track `track`'s rows, paths and meshes from the game's containers: gfx = GFX/MAP<n>H01.DAT (the meshes), data = DATA/MAP<n>.DAT (MAP<n>DAT.DTR, MAP<n>.PTH), both as the
   viewer read them whole. The meshes' materials are the track's MISC.MLB's, which the image holds in front of its MLB: misc_delta is what the builder reports
   (track_build_info.misc_delta; 0: the image has none, and no mesh can be drawn). scratch (cap bytes, 4-aligned; at least 24 KB) is used while it runs. The rows exist as of the
   next s2anim_begin(). Returns the rows (0: none, or the data is not there or beyond the limits; then the track has no animated scenery). */
int s2anim_load(int track, const uint8_t *gfx, uint32_t gfx_size, const uint8_t *data, uint32_t data_size, int32_t misc_delta, uint8_t *scratch, uint32_t cap);

/* A race begins: the rows as the original sets them up (0x31060): each at the start of its path, the ones that start by the player's coming near not started */
void s2anim_begin(void);

/* One frame (0x2cee4): the race clock in seconds, the player's place in whole units and his lap counter (his car's record +0x4a8: -1 on the grid, 0 on the first lap).
   Which rows are shown and heard, and where they are, is then as accessible below. */
void s2anim_frame(float clock, int px, int py, int lap);

/* The objects to draw (at most max): every row that is shown, as a track object for track_draw_cam(); their blocks are made when the drawing comes to them. Returns how many. */
int s2anim_objects(struct track_object *objs, int max);

/* The rows' sounds: how many rows there are, and for row i whether its sound is on (its place in x, y and its sample number are then set; the sample is 0x40 and up as in
   the track's AMBIENT table, the caller maps it) */
int s2anim_rows(void);
int s2anim_heard(int i, int *x, int *y, int *sample);

/* A row's state (+0x30 .. +0x48 of the original's row): its place (x, y, height) as whole numbers, the share of its run (-1: not on its path), the race clock at which the run
   started (0: not), whether its object is shown and whether its sound is on */
struct s2anim_row {
    int32_t place[3];
    float part, started;
    int shown, heard;
};
const struct s2anim_row *s2anim_row(int i);
/* the heading and the climb of row i at its share `part` of the run, in 2048ths of a turn: 0x2cee4's from the path's way there (the drawing works them out for an object
   when it comes up; they are also what the original puts in its object's +0x18 and +0x14) */
void s2anim_angles(int i, float part, int *heading, int *climb);

/* ------------------------------------------------------------------------------------------------ cubic paths of any file
   The track's paths (MAPn.PTH) are read and evaluated by the routines below, and so can any other file of that format: the ARCADE title's cars and camera (DATA/GEN.DAT's SCENEn.PTH,
   s2arcscene.h) keep a set of their own, so that the scenery of the race in the track's set is not touched. A set is the keys of its paths' three channels (x, y and height) and the paths. */
struct s2anim_key { float t, v, out, in; };
struct s2anim_chan { uint16_t first, n; };                              /* its keys: the first in the pool, how many */
struct s2anim_path { struct s2anim_chan c[3]; float start, end; uint8_t ok; };     /* the channels, and the share of the run the path's keys span (head +0x28, +0x2c) */
struct s2anim_paths {
    struct s2anim_key pool[S2ANIM_POOL];
    struct s2anim_path path[S2ANIM_PATHS];
    uint16_t used;                                                      /* keys in the pool */
};

/* An empty set */
void s2anim_paths_clear(struct s2anim_paths *ps);
/* The paths of a file (size bytes at file) into the set: path i of the file into slot base + slot_of(its 100-byte header), a MAPn.PTH's own rule (the name's third character: "MV_<c>", '1' .. '9' then
   'a' on) if slot_of is 0. Slots outside 0 .. S2ANIM_PATHS - 1 are left out. More files can follow into other slots. 1 if all of the file was read (the set's pool and the limits of a channel's keys
   hold it), 0 if not: the set is then not to be used */
int s2anim_paths_load(struct s2anim_paths *ps, const uint8_t *file, uint32_t size, int base, int (*slot_of)(const uint8_t *header));
/* A path's place (x, y, height) at part (0 .. 1) of its run (0x129d5: the cubic between the keys, the first / last key's value outside them) and, if way is not 0, the unit vector of its direction */
void s2anim_paths_at(const struct s2anim_paths *ps, int slot, float part, float place[3], float way[3]);
/* The heading and the climb at part, in 2048ths of a turn (s2anim_angles' rules) */
void s2anim_paths_angles(const struct s2anim_paths *ps, int slot, float part, int *heading, int *climb);
