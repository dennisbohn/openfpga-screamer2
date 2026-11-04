/*
 * Frames of a Screamer 2 track image from the firmware's own code (road.c, track.c, car.c, geom.c, setup.c,
 * raster.c, blit.c; the C models of the units) on the PC, from camera positions on the road or given freely.
 * Built and run by tools/s2/render_s2.sh. Not part of the firmware.
 *
 * Usage: render_s2 <track image> <out prefix> <camera>...
 *   camera: v<s>[,<cars>]   the viewer's scene (firmware/road.c) s units along the road loop: the near chase
 *                           camera over the player's car, cars = 0, 1 or 4 (default 4); s may be followed by
 *                           'f' as a fraction of the loop (v0.25f = a quarter of the way)
 *           r<i>            road point i of the map (MMP array 2, in the map's order), the camera over the
 *                           drivable surface there, looking along the road's heading
 *           c<x>,<y>,<h>,<yaw>,<pitch>   free camera (yaw 0 = towards -y, radians)
 *           g<k>[,<n>]      the race (firmware/race.c) for k steps, a table every n (RACE_GAS=1: the player holds the gas, RACE_ROBOT=1: the game's driver drives him), the
 *                           race scene at the end
 *           d<k>            the lone car (firmware/s2car.h: the physics, or the stand-in) after k steps of 20 ms from the
 *                           start on a script of its own (gas to step 130, left from 25 to 100,
 *                           right to 120, then the brake), the physics' chase camera over its car
 * Writes <prefix><k>.rgb (320 x 200 x 3) per camera and prints what the renderer reports.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "hw.h"
#include "track.h"
#include "blit.h"
#include "road.h"
#include "s2car.h"
#include "race.h"
#include "car.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))      /* as the viewer */

static void map(void)
{
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE)
        || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)(uintptr_t)SDRAM_UBASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        exit(1);
    }
}

static void write_rgb(const char *path, const uint32_t *pal)
{
    volatile uint8_t *fb = FB(0);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t c = pal[fb[i]];
        uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* The cars as they are drawn must hold together. Each car model of the image is drawn with and without the wheels' turn and
   steering: only the four hubs may change with the turn and only the four front wheel blocks with the steering (a body block
   that turns with the wheels, as AUTO02's and AUTO03's did when wheels were taken to be the blocks off the middle, shows as a
   changed block). Returns the number of failures. */
static int check_models(void)
{
    int nm, bad = 0;
    track_models(&nm);
    const struct track_cam cam = { 0.0f, 600.0f, 300.0f, 0.0f, -0.2f, 0.0f };      /* close: every block at its level 0 */
    for (int m = 0; m < nm; m++) {
        static struct track_block base[CAR_BLOCKS], turned[CAR_BLOCKS], steered[CAR_BLOCKS];
        struct track_object ob;
        struct car_look lk = { 0, 0, 0, 0, 0.1f, 0.05f, 0.02f, 0.03f, 0.0f, 0.0f, 0 };
        int nb = car_draw(m, &lk, &cam, base, &ob) ? ob.n : 0;
        lk.spin = 1.1f;
        int nt = car_draw(m, &lk, &cam, turned, &ob) ? ob.n : 0;
        lk.spin = 0.0f;
        lk.steer = 0.3f;
        int ns = car_draw(m, &lk, &cam, steered, &ob) ? ob.n : 0;
        int changed_spin = 0, changed_steer = 0;
        for (int i = 0; i < nb; i++)
            for (int j = 0; j < nt; j++)
                if (turned[j].blk == base[i].blk && memcmp(turned[j].m, base[i].m, sizeof base[i].m)) changed_spin++;
        for (int i = 0; i < nb; i++)
            for (int j = 0; j < ns; j++)
                if (steered[j].blk == base[i].blk && memcmp(steered[j].m, base[i].m, sizeof base[i].m)) changed_steer++;
        printf("car model %d: %d blocks, %d turn with the wheels' spin, %d with the steering\n", m + 1, nb, changed_spin, changed_steer);
        if (nb != nt || nb != ns || changed_spin != 4 || changed_steer != 4) { printf("  FAIL: expected 4 and 4\n"); bad++; }
    }
    return bad;
}

/* a pose that is not a car's: not a number, or tilted or leant further than a car can be */
static int look_ok(const struct car_look *lk)
{
    const float v[10] = { lk->x, lk->y, lk->z, lk->yaw, lk->pitch, lk->roll, lk->body_pitch, lk->body_roll, lk->spin, lk->steer };
    for (int k = 0; k < 10; k++) if (!isfinite(v[k])) return 0;
    float steer = fmodf(lk->steer, 6.2831853f);             /* (the wheels' angle is kept in 0 .. 2 pi: -6.28 is straight) */
    if (steer > 3.1415927f) steer -= 6.2831853f;
    if (steer < -3.1415927f) steer += 6.2831853f;
    return fabsf(lk->pitch) < 1.2f && fabsf(lk->roll) < 1.2f && fabsf(lk->body_pitch) < 0.5f && fabsf(lk->body_roll) < 0.5f && fabsf(steer) < 1.0f;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: render_s2 <image> <prefix> <v<s>[,<cars>]|r<i>|c<x>,<y>,<h>,<yaw>,<pitch>>...\n"); return 2; }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    if (!track_open(TEX_FILE)) { fprintf(stderr, "track_open: not a track image (%zu bytes)\n", n); return 1; }
    int pieces = track_init();
    if (!pieces) { fprintf(stderr, "track_init: the image does not fit the limits (objects > MAX_OBJ or pieces > MAX_PIECES)\n"); return 1; }
    int loop = road_build();
    printf("image %zu bytes, %d placements, road loop %d points, %.0f units\n", n, pieces, loop, road_length());
    int failures = check_models();

    for (int a = 3; a < argc; a++) {
        struct track_cam cam;
        static struct track_object objs[ROAD_CARS];
        int nobj = 0;
        if (argv[a][0] == 'v') {
            char *end;
            double s = strtod(argv[a] + 1, &end);
            int ncars = 4;
            if (*end == 'f') { s *= road_length(); end++; }
            if (*end == ',') ncars = atoi(end + 1);
            nobj = road_scene((float)s, ncars, &cam, objs);
        } else if (argv[a][0] == 'd') {
            static struct s2car_state car;
            int steps = atoi(argv[a] + 1);
            int tn = 1;                                 /* the track of the image: the last digit 1 .. 7 of its file name (ref3.bin), else 1 */
            for (const char *q = argv[1]; *q; q++) if (*q >= '1' && *q <= '7') tn = *q - '0';
            s2car_init(&car, 0, 0, tn, 0);
            for (int k = 0; k < steps; k++) {
                struct s2car_input in = { .throttle = k < 130, .brake = k >= 130, .steer = k >= 100 && k < 120 ? 1.0f : k >= 25 && k < 100 ? -1.0f : 0.0f };
                s2car_step(&car, &in);
                if (k % 25 == 0 || k == steps - 1)
                    printf("step %d: %.1f mph gear %d yaw %.2f at %.0f %.0f %.0f%s\n", k + 1, car.speed_mph, car.gear, car.yaw,
                           car.pos[0], car.pos[1], car.pos[2], car.off_track ? " (off the track)" : "");
            }
            nobj = road_drive_scene(&car, &cam, objs);
        } else if (argv[a][0] == 'g') {
            /* the race (firmware/race.c) for k steps, the player holding the gas if RACE_GAS is set, else parked on the grid;
               every `every` steps (default 250) a line per car: lap done, place, mph, node along the line, place on the map;
               a line when a car finishes a lap */
            int steps = atoi(argv[a] + 1), every = 250;
            const char *comma = strchr(argv[a], ',');
            if (comma) every = atoi(comma + 1);
            int tn = 1;
            for (const char *q = argv[1]; *q; q++) if (*q >= '1' && *q <= '7') tn = *q - '0';
            if (!race_init(tn, road_floor() - 3000.0f)) { fprintf(stderr, "race_init failed\n"); return 1; }
            int laps_seen[RACE_CARS] = { 0 };
            if (getenv("RACE_ROBOT")) {                 /* the game's driver drives the player's car too (it is marked finished with no end to its time) */
                struct s2car_state *p = race_car(0);
                float forever = 1e9f;
                p->rec[0x4c / 4] |= 1;
                memcpy(&p->rec[1], &forever, 4);
            }
            for (int k = 0; k < steps; k++) {
                struct s2car_input in = { .throttle = getenv("RACE_GAS") != 0 };
                race_step(&in);
                for (int i = 0; i < race_cars(); i++) {
                    const struct s2car_state *c = race_car(i);
                    struct car_look lk;
                    s2car_look(c, &lk);
                    if (!look_ok(&lk) && failures++ < 5)
                        printf("  FAIL: car %d at step %d is drawn in a pose no car has (pitch %.2f roll %.2f lean %.2f %.2f steer %.2f)\n", i, k + 1,
                               lk.pitch, lk.roll, lk.body_pitch, lk.body_roll, lk.steer);
                    int lap = (int)c->rec[0x4ac / 4];
                    if (lap != laps_seen[i]) {
                        float best; memcpy(&best, &c->rec[0xc40 / 4], 4);
                        printf("  car %d: lap %d done at %.2f s (best %.2f)%s\n", i, lap, (float)(k + 1) * 0.02f, best,
                               (c->rec[0x4c / 4] & 2) ? " FINISHED" : "");
                        laps_seen[i] = lap;
                    }
                }
                if (k % every == 0 || k == steps - 1) {
                    printf("step %d (%.1f s):", k + 1, (float)(k + 1) * 0.02f);
                    for (int i = 0; i < race_cars(); i++) {
                        const struct s2car_state *c = race_car(i);
                        float dist; memcpy(&dist, &c->rec[0x4b8 / 4], 4);
                        printf("  [%d] L%d P%d %3.0fmph n%.1f%s", i, (int)c->rec[0x4ac / 4], (int)c->rec[0xc30 / 4], c->speed_mph, dist, c->off_track ? "!" : "");
                    }
                    printf("\n");
                }
            }
            struct race_hud h;
            race_get_hud(&h);
            printf("player: lap %d/%d place %d/%d %s time %.2f s lap %.2f best %.2f; the line is %d units\n", h.lap, h.laps, h.place, h.cars,
                   h.finished ? "FINISHED" : "racing", h.time, h.lap_time, h.best_lap, race_line_length());
            printf("cars put back on the line: %u\n", (unsigned)race_respawns());
            struct s2car_state *rc[RACE_CARS];
            for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
            nobj = road_race_scene(rc, race_cars(), &cam, objs);
            if (getenv("RACE_CAM")) {                   /* the view from behind car i (RACE_CAM=i), to look at a computer's car close */
                const struct s2car_state *v = race_car(atoi(getenv("RACE_CAM")));
                cam = (struct track_cam){ v->pos[0] - sinf(v->yaw) * 1000.0f, v->pos[1] + cosf(v->yaw) * 1000.0f, v->pos[2] + 350.0f, v->yaw, -0.2f, 0.0f };
            }
        } else if (argv[a][0] == 'r') {
            int nd;
            const struct track_dir *d = track_dirs(&nd);
            const struct track_dir *q = &d[atoi(argv[a] + 1) % nd];
            float h = 0.0f;
            int grass;
            if (!track_under((float)q->x, (float)q->y, &h, &grass)) fprintf(stderr, "(nothing under road point %s)\n", argv[a] + 1);
            cam = (struct track_cam){ (float)q->x, (float)q->y, h + 600.0f, q->angle, -0.1f, 0.0f };
        } else {
            float x, y, h, yaw, pitch;
            if (sscanf(argv[a] + 1, "%f,%f,%f,%f,%f", &x, &y, &h, &yaw, &pitch) != 5) { fprintf(stderr, "bad camera %s\n", argv[a]); return 2; }
            cam = (struct track_cam){ x, y, h, yaw, pitch, 0.0f };
        }
        const struct rs_clip clip = { 0, 0, FB_W, FB_H };
        volatile uint8_t *fb = FB(0);
        static struct blit_list l;
        blit_start(&l, 0);
        track_backdrop_blit(&l, &clip, &cam);
        blit_model(fb, &l);
        struct track_stats ts;
        memset(&ts, 0, sizeof ts);
        track_mode = TRACK_MODEL;
        track_draw_cam(fb, &clip, &cam, objs, nobj, &ts);
        char name[512];
        snprintf(name, sizeof name, "%s%d.rgb", argv[2], a - 3);
        write_rgb(name, track_palette());
        printf("camera %.0f %.0f %.0f yaw %.3f: %u pieces handed on, %u faces, %u triangles, %u vertices, %u see-through, %u objects; %s\n",
               cam.x, cam.y, cam.h, cam.yaw, ts.pieces, ts.faces, ts.triangles, ts.vertices, ts.see_through, ts.objects, name);
    }
    if (failures) { printf("%d pose check failures\n", failures); return 3; }
    return 0;
}
