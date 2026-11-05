/*
 * The road of a track as a loop, and the viewer's scene on it (viewer.h). The map's road points
 * (MMP array 2, track.h) are listed cell by cell, so consecutive points are not always neighbours on
 * the road; road_build() chains them: each point leads to the next one along its heading (the next
 * in the list while that lies ahead and close, else the nearest point ahead), from point 0 round to
 * where it began. The height at each point comes from the drivable surface (track_under). The scene
 * is the chase camera of the game's near view over a car standing on the road, with cars ahead of it.
 * No hardware: the PC renderer (tools/s2/render_s2.sh) draws the same scene.
 */
#pragma once
#include <stdint.h>
#include "track.h"

/* Chain the road points of the track set up with track_init() and find their heights. Returns the
   points in the loop, 0 if there are none. Takes some 300 thousand float operations plus one
   track_under() per point. */
int road_build(void);

/* The loop's length in units */
float road_length(void);

/* The lowest height of any road point (the driving mode resets a car that falls far below it) */
float road_floor(void);

/* The place s units along the loop from road point 0 (any s: it wraps, also backwards): x, y, the
   height of the road there and its heading yaw (the car's: forward = (sin yaw, -cos yaw)), between
   the points linearly (the heading along the shorter way round) */
void road_at(float s, float *x, float *y, float *z, float *yaw);

/* The viewer's scene at s units along the road with ncars cars (0, 1 or 4): the near chase camera
   (1300 behind, 300 up, looking down 0.1885 rad) over the player's car, standing on the road at s;
   with 4 cars three more ahead (2,200 units in front, then every 1,700, alternately 500 to either
   side), models AUTO1.. in turn. The cars are objects for track_draw_cam(), at most ROAD_CARS,
   whose blocks are made when they come up in the drawing. Returns the objects. */
#define ROAD_CARS   4
int road_scene(float s, int ncars, struct track_cam *cam, struct track_object *objs);

/* The driving mode's scene: the camera the physics gives (s2car.h) and the player's car as one object for
   track_draw_cam(), model AUTO01 (the physics does not choose a car model yet) with the wheels as the physics
   has them. The state must stay where it is until the frame is drawn (the car's blocks are made then). Returns 1. */
struct s2car_state;
int road_drive_scene(const struct s2car_state *s, struct track_cam *cam, struct track_object *obj);

/* The race's scene: the camera of car 0 (the player's) and the n cars (at most ROAD_CARS) as objects, car k the model
   AUTO(k+1), each as it is now: the poses are copied here, so a car's state may change while the frame is drawn (the computers'
   steps in race.h's slices). Returns the number of objects. */
int road_race_scene(struct s2car_state *const *cars, int n, struct track_cam *cam, struct track_object *objs);
/* A new race: the cars' windows' reflection starts again (the original makes new object records: the first drawing adds the car's whole place
   to the offsets, as track.h's track_refl says) */
void road_race_reset(void);
