/*
 * The ARCADE title's cars (s2arcscene.h)
 *
 * Read from STARTL.EXE 0x3b6e8 (tools/dis.sh -f) and held against the running original (tools/s2/dosbox_scene.py: the camera record 0x131ea4 and the four cars' object
 * records every frame, 45 s, 2,454 frames of the four takes; tools/scene.sh): the page's frame is the picture, the camera made from the take's camera path and its target (the
 * paths' first keys, cut to whole numbers, the look from one at the other as 0x33905 makes it, the angles x 1024 / pi cut and & 0x7ff), the cars placed from (take, clock) by
 * 0x129d5 (the paths' place cut, the heading from the way), the engine's drawing (0x75570), the keys, the clock (t = t + dt x 0.25 in the x87's double, past 1.0 it is 0
 * and the next take, after the fourth the first), the prompt, the fade. The first frame has the clock at 0: the clock goes on after the drawing.
 */
#include "s2arcscene.h"
#include "s2anim.h"
#include "s2scene.h"
#include "s2showroom.h"
#include "dat.h"
#include "dlog.h"
#include <string.h>

#define DT          0.02f                       /* the frame time the page runs at (0x133350 stays at the value the load list sets) */
#define SLOTS       6                           /* a file's paths in order of use: cars 0 .. 3, the camera, its target */
#define SLOT_CAMERA 4
#define SLOT_TARGET 5
#define GEN_MAX     0x2800u                     /* DATA/GEN.DAT: 9,070 bytes */
#define PTH_MAX     4096u                       /* a SCENEn.PTH unpacked: 3,536 to 3,932 */

static struct s2anim_paths paths;               /* the four files' paths, SLOTS each: the track's own set (s2anim.c) is not touched */
static int paths_ok;
static int take;
static float take_clock;                           /* 0 .. 1 over a take */
static struct { int32_t pos[3]; int a1; } car[S2ARCSCENE_CARS];
static struct { int32_t pos[3]; int a0, a1; } cam;
static struct track_cam camera;
static int cam_take = -1;                       /* the take the camera was made for */

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

/* a path's slot in its file (0x351b7): the kind of path at +0x60 (4 a car's, 9 the camera's, 10 its target's: & 7 is what the game tests), a car's number the digit of "MV_car<j>" */
static int scene_slot(const uint8_t *h)
{
    switch (rd32(h + 0x60) & 7) {
    case 4: return h[8 + 6] >= '0' && h[8 + 6] < '0' + S2ARCSCENE_CARS ? h[8 + 6] - '0' : -1;
    case 1: return SLOT_CAMERA;
    case 2: return SLOT_TARGET;
    }
    return -1;
}

/* the four takes' paths out of DATA/GEN.DAT, once; every path a take needs must be there */
static int load_paths(struct zip *z)
{
    static uint32_t gen[GEN_MAX / 4 + 2], file[PTH_MAX / 4];
    if (paths_ok) return 1;
    const struct zip_entry *e = zip_find(z, "DATA/GEN.DAT");
    if (!e || e->size + ZIP_SLACK > sizeof gen || zip_read(z, e, (uint8_t *)gen, sizeof gen)) return 0;
    s2anim_paths_clear(&paths);
    for (int k = 0; k < S2ARCSCENE_TAKES; k++) {
        char name[16] = "SCENE0.PTH";
        struct dat_entry d;
        name[5] = (char)('0' + k);
        if (!dat_find((const uint8_t *)gen, e->size, name, &d)) return 0;
        const uint32_t n = dat_extract((const uint8_t *)gen, e->size, &d, (uint8_t *)file, sizeof file);
        if (!n || !s2anim_paths_load(&paths, (const uint8_t *)file, n, SLOTS * k, scene_slot)) return 0;
        for (int s = 0; s < SLOTS; s++) {
            const struct s2anim_path *p = &paths.path[SLOTS * k + s];
            if (!p->ok || !p->c[0].n || !p->c[1].n || !p->c[2].n || p->end <= p->start) return 0;
        }
    }
    paths_ok = 1;
    return 1;
}

int s2arcscene_load(struct zip *z, int team, int n)
{
    if (!load_paths(z)) {
        dlog_str("arcade: DATA/GEN.DAT's SCENE paths could not be read: the title without the cars\n");
        return 0;
    }
    return s2showroom_load(z, team, n);
}

/* a path's first place as the engine's record has it (x, y up, z): the path's x, height, y */
static void first_place(int slot, int32_t out[3])
{
    float p[3];
    s2anim_paths_at(&paths, slot, 0.0f, p, 0);
    out[0] = (int32_t)p[0];
    out[1] = (int32_t)p[2];
    out[2] = (int32_t)p[1];
}

void s2arcscene_frame(uint32_t tick)
{
    if (!tick) {
        take = 0;
        take_clock = 0.0f;
        cam_take = -1;
    } else {                                    /* 0x3bb2d: the sum in the x87's double, stored as a float; past 1.0 the next take from 0 */
        take_clock = (float)((double)DT * 0.25 + (double)take_clock);
        if (take_clock > 1.0f) {
            take_clock = 0.0f;
            take = (take + 1) % S2ARCSCENE_TAKES;
        }
    }
    if (!paths_ok) return;
    if (cam_take != take) {                     /* the camera: the take's camera path's place, the look at its target's */
        int32_t aim[3];
        float pitch, head;
        first_place(SLOTS * take + SLOT_CAMERA, cam.pos);
        first_place(SLOTS * take + SLOT_TARGET, aim);
        s2scene_look_at(cam.pos, aim, &pitch, &head);
        cam.a0 = s2scene_angle(pitch);
        cam.a1 = s2scene_angle(head);
        camera = s2scene_camera_of(cam.pos, cam.a0, cam.a1);
        cam_take = take;
    }
    for (int i = 0; i < S2ARCSCENE_CARS; i++) {
        float p[3];
        int heading, climb;
        s2anim_paths_at(&paths, SLOTS * take + i, take_clock, p, 0);
        s2anim_paths_angles(&paths, SLOTS * take + i, take_clock, &heading, &climb);
        car[i].pos[0] = (int32_t)p[0];
        car[i].pos[1] = (int32_t)p[2];
        car[i].pos[2] = (int32_t)p[1];
        car[i].a1 = heading;
        s2showroom_place(i, car[i].pos, heading);
    }
}

void s2arcscene_draw(struct blit_list *l)
{
    s2showroom_draw_cam(l, &camera);
}

int s2arcscene_take(void)
{
    return take;
}

float s2arcscene_clock(void)
{
    return take_clock;
}

void s2arcscene_car(int i, int32_t pos[3], int *a1)
{
    memcpy(pos, car[i].pos, sizeof car[i].pos);
    *a1 = car[i].a1;
}

struct track_cam s2arcscene_camera(int32_t pos[3], int *a0, int *a1)
{
    memcpy(pos, cam.pos, sizeof cam.pos);
    *a0 = cam.a0;
    *a1 = cam.a1;
    return camera;
}
