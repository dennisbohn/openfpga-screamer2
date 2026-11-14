/*
 * A car's model as an object for the drawing: its blocks made in the game's order for the camera,
 * the wheels turned and steered, the body leant; and a flat mesh or any mesh placed as a car's body
 * is. After OpenScreamer's port/src/main.c (GPL-3.0: wheels_step, draw_object), which took it from
 * the game. The cars' physics is s2car.h.
 */
#pragma once
#include <stdint.h>
#include "track.h"

/* Any car as an object: car model `model` of the track image (track_models()) drawn at place
   x, y, z, heading yaw, the object's pitch and roll (the car's negated), its body's own lean
   on top (not the wheels'), the wheels' turn and the front ones' angle, the texture phase
   (geom.h). Its blocks go into out (CAR_BLOCKS), in the game's order for the camera. */
#define CAR_BLOCKS  16
struct car_look { float x, y, z, yaw, pitch, roll, body_pitch, body_roll, spin, steer; uint32_t phase; };
/* Forget the car models made from the track image set up before (after each track_init) */
void car_models_reset(void);
int car_draw(int model, const struct car_look *lk, const struct track_cam *cam, struct track_block *out,
             struct track_object *obj);
/* The same for a car that more than one pass draws in a frame (the picture's and the mirror's): what does not depend on the camera -- the trigonometry of the look, the
   blocks' middles and places in the world -- is worked out by the first pass and kept in *pose. The caller owns the pose and clears `made` whenever the look changes. */
struct car_trig { float sc[4], car[6], body[4]; int lean; float x, y, z, drop; };      /* a car as it is drawn (car.c): the cosines and sines of the wheels' turn and the front
                                                                                           ones' angle, of the object's pitch, roll and heading, of the body's own pitch and roll,
                                                                                           whether the body leans, the place, how far the body is dropped */
struct car_pose {
    int made;                           /* ps and mid are worked out */
    uint32_t placed;                    /* bit b: b[b]'s place is */
    struct car_trig ps;
    float mid[CAR_BLOCKS][3];           /* the blocks' middles in the world */
    struct track_block b[CAR_BLOCKS];
};
int car_draw_pose(int model, const struct car_look *lk, const struct track_cam *cam, struct car_pose *pose,
                  struct track_block *out, struct track_object *obj);
/* The same for a car's mesh that is not the track's (a page's car): its blocks into out
   (CAR_BLOCKS) in the game's order for the camera; how many */
int car_mesh_draw(const uint8_t *mesh, const struct car_look *lk, const struct track_cam *cam,
                  struct track_block *out);

/* The first block of a flat mesh (a shadow) placed as a car's body is: at x, y, z, heading yaw,
   tilted by pitch and roll (struct car_look's) */
void car_flat_block(const uint8_t *file, float x, float y, float z, float yaw, float pitch, float roll,
                    struct track_block *out);
/* Every block of a mesh placed so (the helicopter: its body and its rotor), each at its own
   place in the mesh; how many went into out, up to max */
int car_mesh_blocks(const uint8_t *file, float x, float y, float z, float yaw, float pitch, float roll,
                    struct track_block *out, int max);
