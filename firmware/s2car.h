/*
 * The player's car of Screamer 2 on the track: the game's own car model (docs/screamer2/driving.md),
 * ported from OpenScreamer's hand-written routines (its docs/NOTES2.md) to single-precision C. One
 * step is 20 ms (50 Hz); the track is the image set up with track_open() / track_init() (track.h).
 *
 * THIS HEADER IS THE CONTRACT between the physics (s2car.c) and the viewer's driving mode: names and
 * meaning stay as they are. Units are the game's: lengths in the track's units, angles in radians,
 * speeds in km/h as the game counts them (speed x 3600 / 400000 of units a second; mph = km/h x 0.6214).
 *
 * Angles of the car (the game's record +0x4e4 .. +0x4ec):
 *   yaw    the heading: forward = (sin yaw, -cos yaw) in the track's (x, y), as track_cam and car_look
 *   pitch  + nose up
 *   roll   + the car's right-hand side (+X of the game's x) up
 * body_pitch, body_roll: the body's own lean on its springs (small, record +0xc18 / +0xc20), same senses.
 * The wheels are numbered as the game has them: 0 front +X, 1 rear +X, 2 front -X, 3 rear -X.
 */
#pragma once
#include <stdint.h>

struct track_cam;                       /* track.h */
struct car_look;                        /* car.h */
struct track_image;                     /* (opaque, unused: the image of track_open() / track_init() is the one used) */

struct s2car_input {
    float throttle;                     /* 0 .. 1 */
    float brake;                        /* 0 .. 1 */
    float steer;                        /* -1 (left) .. 1 (right); the game's keys give +-1 and ease */
    int gear_up, gear_down;             /* edge-triggered (1 for the one step on which the button went down); used only
                                           with s2car_state.manual (the gearbox of the player's car is automatic by default) */
    int handbrake;                      /* 0 / 1 */
};

struct s2car_state {
    /* ---- output: read after s2car_step() ---- */
    float pos[3];                       /* x, y, height (up), as track.c / car_look use them */
    float yaw, pitch, roll;             /* see above */
    float body_pitch, body_roll;
    float rot[9];                       /* the orientation as rows of the world axes (x, y, up) in the car's
                                           (right, up, forward) columns: world = o + rot . local (track_block.m) */
    float vel[3];                       /* world units a second (x, y, up) */
    float speed_kmh, speed_mph;         /* speed along the ground */
    float rpm;                          /* the engine's revs in the game's units (most = max_rpm of CAR_PARAMS) */
    int gear;                           /* 0 neutral, 1 .. */
    float wheel_steer;                  /* the front wheels' angle on the car, radians (+ towards +X, to the right) */
    float wheel_spin[4];                /* cumulative turn of each wheel in radians (rolls forward when growing) */
    float susp[4];                      /* each wheel's spring compression in units (0 at rest) */
    int on_ground;                      /* wheels on the ground, 0 .. 4 */
    int skid_flags;                     /* bit w set: wheel w skids (smoke); bit 4: the tyres squeal */
    int hit_wall;                       /* 1 on a step on which the car touched a wall */
    float hit_hard;                     /* the strongest wall knock (the game's 0x29571 `hard`, which its sound goes by) since the caller
                                           last set it to 0; set by the physics only upwards */
    uint32_t look;                      /* the car object's flags (0x16878, record +0x20), set once a step with the wheels: 0x200 the
                                           brake is down (the lights), 0x400 / 0x8000 a pair of wheels turning fast (the hubs' blur),
                                           the damage marks of the four sides 0x2000 0x800 0x4000 0x1000 (record +0xb8); the texture
                                           picture of a car's materials is look >> 9 (128 pictures) */
    int off_track;                      /* 1 if the car's middle has no drivable ground under it (for a reset rule) */
    float cam[6];                       /* the game's chase camera (view 3): x, y, height, yaw, pitch, roll, as track_cam */
    /* ---- input to the physics from the caller ---- */
    int manual;                         /* 1: gears by gear_up / gear_down (the game's MANUAL option); default 0 */
    int stick;                          /* 1: input.steer is an analog axis (the game's control type 0: its curve, and a
                                           car that steers 1.3 times as far); 0, the default: the d-pad's keys, the whole
                                           lock to the side held (type 1). Either may change from step to step. */
    int ai;                             /* 1: a computer's car (the game's car without a controls record, +0xb4 = 0): its own
                                           grip, wall rule, set-up and pace handicap; set it with s2car_init_grid() */
    int cam_on;                         /* 1: a computer's car (ai) keeps its chase camera worked out in cam[] as the player's does: the replay's cameras follow it */
    int hold;                           /* 1: the car waits on the grid, the gearbox in neutral (the game's race flag
                                           0x11f0a8 & 0x20 not yet set); the caller clears it when the race is let go */
    /* ---- private ---- */
    uint32_t rec[0xcc0 / 4];            /* the game's car record, word for word where the port keeps its layout */
    float camst[5];                     /* the chase camera's eased distance, pitch, yaw, roll and height */
    int cam_started;
    int squeal;                         /* the tyres squeal (set by the step on the ground, read by the outputs) */
};

/* GAME SETTINGS' DAMAGES (the original's 0x130d54): 0 and a knock from a wall marks nothing and hurts nothing (0x19aaa returns at once), 1 the
   marks and the damage. 1 until a menu sets it (the tools and tests), the viewer sets it from the save when a race is set up. */
extern int s2car_damages;

/* Car number 0 .. 4 of team 0 .. 3 (the fifth is the team's bonus car) on track 1 .. 7: CAR_PARAMS
   read (s2car_params()), the car put on the start line, heading along the road, wheels on the ground,
   the race clock running (the 3 s of the grid hold are the caller's business). img may be NULL.
   1 if it worked, 0 if the track has no start or the params are missing. */
int s2car_init(struct s2car_state *s, int team, int car, int track, const struct track_image *img);

/* The same on a place of the grid (the game's 0x2f519): grid place 0 is the front row; place g is 1000 units to one
   side of the first node of the line and 2000 x g behind it, the sides in turn. A race of n cars gives car number i
   the place n - 1 - i (the player, car 0, the last). ai = 1 makes it a computer's car (s2car_state.ai). The car is
   held on the grid (s2car_state.hold = 1) until the caller clears it. */
int s2car_init_grid(struct s2car_state *s, int team, int car, int track, int grid, int ai);

/* The same abreast, as time attack and the free run put the cars (0x30328 .. 0x303fa: no row behind): side 0 and side 1 are 1000 units to either side
   of the line's first node (the sides in turn as the grid's places 0 and 1 are, in the same row); the original gives car i of n the side n - 1 - i. */
int s2car_init_row(struct s2car_state *s, int team, int car, int track, int side, int ai);

/* The car put on the line itself at (x, z), heading along the road there, standing, held (hold = 1): a car that left the
   road is put back (the caller keeps what it knows of the car, the record is made new). 1 if it worked. */
int s2car_init_at(struct s2car_state *s, int team, int car, int track, float x, float z, int ai);

/* One step of a car that a driver drives (the game's driver 0x17726 for the computer's cars, and for the player's
   after the finish): the physics up to the controls as s2car_step(), then driver(s, ctx) sets the controls in the
   record (throttle word 0x1ec, brake 0x1e4 / 0x1e8, the front wheels' angle 0x660), then the steering as the game
   works it out from them. The gearbox is the car's own (automatic). */
void s2car_step_driven(struct s2car_state *s, void (*driver)(struct s2car_state *s, void *ctx), void *ctx);

/* The grip of the ground under the four wheels for these tyres, as the driver counts it (1 - ... in 0x17726): the mean of
   the road kinds' tyre grip */
float s2car_ground_grip(const struct s2car_state *s);

/* The DATA/MAPn.DAT container of the last s2car_init (the first node of the car's line is read from it; race.c reads
   the whole line from it): NULL if none */
const uint8_t *s2car_map_dat(uint32_t *size);
/* and DATA/GAME.DAT (CARS.DTR, MISC.DTR) */
const uint8_t *s2car_game_dat(uint32_t *size);

/* The game's containers the car's numbers come from: DATA/GAME.DAT (CARS.DTR) and DATA/MAPn.DAT of the track
   (MAPnDAT.DTR, MAPnOPP.SPL). On the Pocket s2car_init() reads them from the ZIP itself (files_zip(), a few
   KB); the host tests hand them over here before s2car_init(). */
void s2car_set_data(const uint8_t *game_dat, uint32_t game_size, const uint8_t *map_dat, uint32_t map_size);

/* ONE 20 ms step of the car; fills the outputs. */
void s2car_step(struct s2car_state *s, const struct s2car_input *in);

/* The step in the game's order, which cars touching need (race.c: the solver 0x245a3 comes between the passes over all the cars and what is done
   with the controls). s2car_step() is s2car_move() and s2car_finish(), s2car_step_driven() s2car_move() and s2car_finish_driven().
   s2car_move(): the tyres, the walls and the ground, the step on the ground (0x2baa4's first three passes); driven: a driver's car (the gearbox is
   automatic whatever .manual says). s2car_move_again(): the same again for dt, what is left of the step after a touch, from the place the touch put the
   car at (0x2baa4's second pass; the flag +0x4c bit 8 set by the solver stays), s2car_walls() the walls and ground alone (0x16824, which the game runs
   again for every car at that point). s2car_finish(): the rest of the step: the wheels' picture, the clocks, the engine, the controls, the outputs. */
void s2car_move(struct s2car_state *s, int driven);
void s2car_move_again(struct s2car_state *s, float dt);
void s2car_walls(struct s2car_state *s);
void s2car_finish(struct s2car_state *s, const struct s2car_input *in);
void s2car_finish_driven(struct s2car_state *s, void (*driver)(struct s2car_state *s, void *ctx), void *ctx);

/* A knock on side 0 .. 3 at corner 0 .. 3 (0x19aaa: the side's count and mark, with DAMAGES on the corner's and the car's damage), share of the edge 0 .. 1, force,
   the damage's scale for the level (MISC.DTR's DAMAGES: pro 5, normal 2.5, rookie 1; the walls' knocks of the player's car are not scaled); and
   where two lines meet (0x20970: the line through p along d and the one through q along e, x at [0] and z at [2]; the distances along each; 0 when they
   run side by side). The touching cars' solver and push use them. */
void s2car_knock(struct s2car_state *s, int side, int corner, float share, float force, float scale);
int s2car_lines_meet(const float p[3], const float d[3], const float q[3], const float e[3], float *s, float *u);

/* A place after the game's cut to whole numbers towards zero (the game's fistp in chop mode), or not: the header of s2car.c says why the firmware keeps the
   fraction (S2_WHOLE_PLACES=1 cuts it, as the host tests do to follow the reference to the unit) */
#ifndef S2_WHOLE_PLACES
#define S2_WHOLE_PLACES 0
#endif
static inline float s2car_placed(float f)
{
    if(!S2_WHOLE_PLACES) return f;
    if(!(f > -2147483648.0f)) return -2147483648.0f;
    if(f >= 2147483648.0f) return 2147483520.0f;
    return (float)(int32_t)f;
}

/* The car as car.h draws it (struct car_look): place, heading, tilt, body lean, the wheels' turn and
   steering, the texture phase (look >> 2). */
void s2car_look(const struct s2car_state *s, struct car_look *lk);

/* The picture the car's windows show: the object's +0x3c (0x1737b copies the car's +0xc80), the phase of the reflection's picture (frame n x phase >> 16 of
   ENV.MLB's first material), by the kind of the ground under the car's fourth wheel (record +0x6b0): s2_env_phase() in s2env.h. *rate (if not 0): what the player's
   car alone sets, how fast the reflection slides across the glass as an object moves (track_reflection_rate()). */
uint32_t s2car_reflection(const struct s2car_state *s, float *rate);

/* A test of the reference (OpenScreamer's s2_put): the car at (x, y), height 0, facing heading, its corners worked out
   and the last frame's record made the same. */
void s2car_put(struct s2car_state *s, int x, int y, float heading);

/* The chase camera as a track_cam (the same as cam[]) */
void s2car_camera(const struct s2car_state *s, struct track_cam *cam);

/* What the effects of s2fx.h read of a car after its step (the game's record +0x4d8 .. +0x510, +0x574 .. +0x6c4, +0xc0c ..; the wheels' and corners' words are the physics' own):
   the smoke's rule (0x204af) looks at the rear wheels 1 and 3 with the road kinds of the wheels 0 and 2 beside them, the shadow's (0x1f4a7) at the ground under the car */
struct s2car_fx {
    int on_ground;                      /* the engine / tyres' flag +0x510 is above 0: the car is on the ground */
    float slip;                         /* +0xc34: 1 grips, below 0.93 the tyres slide */
    float speed;                        /* +0x500, in the physics' units (the smoke goes by 20) */
    int x, y, z;                        /* the car's place cut to whole numbers (the game's object record, [car +0x990]: what its x & 7 and z & 7 are), y the height */
    float yaw, pitch;                   /* +0x4e8, +0x4e4 */
    struct {
        int flags, dispersion;          /* the road kind's smoke flags (1 asmoke, 2 a2smoke, 4 smokes at speed too) and dispersion */
        int world[3];                   /* the rear corner's place: x, the ground's height under it, z (whole numbers) */
        int local[3];                   /* the wheel's place in the car (x, up, z) */
    } rear[2];
    int ground;                         /* +0x4f8: the ground's height under the car (the mean of its four corners') */
    float slope_along, slope_across;    /* +0xc0c, +0xc14 */
};
void s2car_fx(const struct s2car_state *s, struct s2car_fx *out);

/* The ROAD KINDS row of the track's kind id (its smoke[2] and smoke[3] number the tyres' rolling and squealing
   sounds, s2snd.h); a row of zeros for any the track does not list */
struct s2_road_kind;
const struct s2_road_kind *s2car_road_kind(int id);
