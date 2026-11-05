/*
 * A kinematic STAND-IN for the Screamer 2 car physics (s2car.h), so that the viewer's driving mode runs
 * before the real physics (s2car.c) is there. The Makefile uses this file only while firmware/s2car.c
 * does not exist. It is not a model of the game's car:
 *   - speed: the throttle pushes towards the top speed of the gear (200 km/h in the automatic box), the
 *     brake and the handbrake take speed away, a little drag always; no reverse, no slip, no weight
 *   - heading: the front wheels follow the steering with a lag, the yaw rate grows with the wheel angle
 *     and the speed (no limit of grip)
 *   - the car sits on the drivable surface under it (track_under) and is tilted by it; where there is
 *     none, it keeps its height (off_track = 1) and drives on
 *   - gears: five, 40 km/h each; automatic by speed, or by gear_up / gear_down when s->manual
 *   - camera: behind and above as the viewer's ride mode has it (road.c), the heading eased
 * It keeps whether the brake was on in rec[0].
 */
#include "s2car.h"
#include "car.h"
#include "track.h"
#include <math.h>
#include <string.h>

#define DT          0.02f
#define UNITS_KMH   111.11f             /* units a second for 1 km/h (s2car.h: km/h = speed x 3600 / 400000) */
#define GEARS       5
#define GEAR_KMH    40.0f               /* the speed a gear reaches */
#define WHEEL_R     120.0f              /* units */
#define WHEEL_MAX   0.35f               /* radians */
#define ACCEL       60.0f               /* km/h a second from a standstill, less towards the top speed */
#define YAW_RATE    1.2f                /* radians a second at full lock, at speed */
#define YAW_KNEE    40.0f               /* km/h at which the yaw rate is half of that */
#define PI          3.14159265f

static float wrap(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

/* the car on the surface under it, and the orientation matrix of its angles */
static void ground(struct s2car_state *s)
{
    float h;
    int grass;
    s->off_track = !track_under(s->pos[0], s->pos[1], &h, &grass);
    s->on_ground = s->off_track ? 0 : 4;
    if (!s->off_track) {
        s->pos[2] = h;
        track_under_tilt(sinf(s->yaw), -cosf(s->yaw), &s->pitch, &s->roll);
    }
    /* rot as track_block.m has it for the car (car.c block_point without wheels and lean): the images of the
       block axes x, y (up), z in the world, the object's pitch and roll being the car's, negated */
    float cy = cosf(s->yaw), sy = sinf(s->yaw), cp = cosf(-s->pitch), sp = sinf(-s->pitch);
    float cr = cosf(-s->roll), sr = sinf(-s->roll);
    const float e[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    for (int c = 0; c < 3; c++) {
        float y3 = e[c][1] * cp + e[c][2] * sp, z3 = e[c][2] * cp - e[c][1] * sp;
        float x3 = e[c][0] * cr - y3 * sr, y4 = y3 * cr + e[c][0] * sr;
        s->rot[c] = x3 * cy - z3 * sy;
        s->rot[3 + c] = x3 * sy + z3 * cy;
        s->rot[6 + c] = y4;
    }
}

/* the chase camera: 1300 behind, 300 up, looking down 0.1885 rad, the heading eased */
static void camera(struct s2car_state *s)
{
    const float behind = 1300.0f, above = 300.0f, pitch = -0.1885f;
    s->cam_yaw = s->cam_started ? s->cam_yaw + wrap(s->yaw - s->cam_yaw) * 0.12f : s->yaw;
    s->cam_started = 1;
    s->cam[0] = s->pos[0] - cosf(pitch) * behind * sinf(s->cam_yaw);
    s->cam[1] = s->pos[1] + cosf(pitch) * behind * cosf(s->cam_yaw);
    s->cam[2] = s->pos[2] + above - behind * sinf(pitch);
    s->cam[3] = s->cam_yaw;
    s->cam[4] = pitch;
    s->cam[5] = 0.0f;
}

int s2car_init(struct s2car_state *s, int team, int car, int track, const struct track_image *img)
{
    (void)team;
    (void)car;
    (void)track;
    (void)img;
    memset(s, 0, sizeof *s);
    track_start(&s->pos[0], &s->pos[1], &s->yaw);
    s->gear = 1;
    s->rpm = 1000.0f;
    ground(s);
    camera(s);
    return 1;
}

void s2car_step(struct s2car_state *s, const struct s2car_input *in)
{
    if (s->manual) {
        s->gear += (in->gear_up && s->gear < GEARS) - (in->gear_down && s->gear > 1);
    } else {
        s->gear = 1 + (int)(s->speed_kmh / GEAR_KMH);
        if (s->gear > GEARS) s->gear = GEARS;
    }
    float top = GEAR_KMH * (float)(s->manual ? s->gear : GEARS), v = s->speed_kmh;
    if (v < top) v += ACCEL * in->throttle * (1.0f - v / top) * DT;        /* about 100 km/h in 2.3 s in the box */
    v -= (4.0f + 0.02f * v + 120.0f * in->brake + 80.0f * (float)in->handbrake) * DT;
    if (v < 0.0f) v = 0.0f;
    s->speed_kmh = v;
    s->speed_mph = v * 0.6214f;
    s->rpm = 1000.0f + 7000.0f * v / (GEAR_KMH * (float)s->gear);
    if (s->rpm > 8000.0f) s->rpm = 8000.0f;

    s->wheel_steer += (in->steer * WHEEL_MAX - s->wheel_steer) * 0.25f;
    s->yaw = wrap(s->yaw - s->wheel_steer / WHEEL_MAX * YAW_RATE * v / (v + YAW_KNEE) * DT);   /* yaw grows to the left */
    float u = v * UNITS_KMH, z0 = s->pos[2];
    s->vel[0] = sinf(s->yaw) * u;
    s->vel[1] = -cosf(s->yaw) * u;
    s->pos[0] += s->vel[0] * DT;
    s->pos[1] += s->vel[1] * DT;
    ground(s);
    s->vel[2] = (s->pos[2] - z0) / DT;
    for (int w = 0; w < 4; w++) s->wheel_spin[w] += u * DT / WHEEL_R;
    s->skid_flags = in->handbrake && v > 10.0f ? 0x0A : 0;       /* the rear wheels */
    s->rec[0] = in->brake > 0.0f;
    camera(s);
}

void s2car_look(const struct s2car_state *s, struct car_look *lk)
{
    /* the object's pitch and roll are the car's, negated; the texture phase as Screamer 2's cars have it (s2car.h look >> 2):
       0x200 while braking, no damage marks, no blur */
    uint32_t phase = (s->rec[0] ? 0x200u : 0u) >> 2;
    *lk = (struct car_look){ s->pos[0], s->pos[1], s->pos[2], s->yaw, -s->pitch, -s->roll, s->body_pitch, s->body_roll,
                             -s->wheel_spin[0], s->wheel_steer, phase };
}

uint32_t s2car_reflection(const struct s2car_state *s, float *rate)
{
    (void)s;
    if (rate) *rate = 0.002f;
    return 0;
}

void s2car_camera(const struct s2car_state *s, struct track_cam *cam)
{
    *cam = (struct track_cam){ s->cam[0], s->cam[1], s->cam[2], s->cam[3], s->cam[4], s->cam[5] };
}
