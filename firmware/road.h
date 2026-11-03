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
