/*
 * Frame statistics along a race (docs/screamer2/performance.md, section 7): the race of firmware/race.c on
 * the PC with the game's driver also driving the player's car (the "race robot" of render_s2.c), the scene
 * of the viewer's race (road_race_scene: the physics' chase camera, the four cars where they are) drawn
 * every `every` steps with the counters of perf_patch.py; one CSV line per frame, as framestats.c has it
 * plus the race (step, the player's node on the line, speed, the cars' places).
 * Built and run by tools/s2/racestats.sh. Not part of the firmware.
 *
 * ATTRACT=1 (menus row 9): the attract race instead: nobody drives car 0 (race_attract: every car the game's driver's), the scene seen from the replay's trackside cameras
 * (s2cam.c, the ten-second rotation of the car followed) instead of the physics' chase camera; the columns px.. are car 0's as before.
 *
 * REPLAY=<file> in the environment: the player's car is driven by the buttons of a Pocket's race instead of the game's driver: the file has lines `I<step> <hex>`
 * as debug.txt has them (the race step a change of the buttons took effect at, the buttons in hex, hw.h's KEY_*; gas B, brake Y, left/right, handbrake A). The race is
 * the same whatever the frames took, so this is that race. FX=1: the shadows and the wheel smoke (s2fx.c) in the scene too, as the viewer has them (columns fx_puffs,
 * and the pieces then include them); the columns yaw and cam_yaw follow the px.. ones.
 *
 * TEAM=<1..4> CAR=<1..5> [SKILL=<0 rookie, 1 normal, 2 pro>] [LAPS=<n>]: the menu's choices (the Pocket's debug.txt: "menu: race T4 CONDOR2 1L ROOKIE AUTO" is
 * TEAM=1 CAR=2 SKILL=0 LAPS=1); the grid is the track image's, so build the image with S2CARS for the same grid (tools/s2/make_track_s2.py), else the pieces differ a little.
 *
 * Usage: racestats <track image> <label> <out.csv> <steps> <every> [track]
 *   track  1 .. 7, default the last digit of the image's file name
 * Needs the game's files for the physics: S2_GAME=<game folder> (racestats.sh mounts it).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "hw.h"
#include "track.h"
#include "car.h"
#include "road.h"
#include "s2car.h"
#include "race.h"
#include "perf.h"
#include "s2cam.h"
#include "s2fx.h"

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

static uint32_t rp_step[2048], rp_keys[2048];             /* REPLAY: the changes of the buttons */
static int rp_n;

static void rp_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[128];
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef";
    unsigned at = 0;
    while (fgets(line, sizeof line, f) && rp_n < 2048) {
        unsigned st, k, d;
        char c;
        if (sscanf(line, "I%u %x", &st, &k) == 2) { rp_step[rp_n] = st; rp_keys[rp_n++] = k; }      /* the first log's lines: step, buttons in hex */
        else if (sscanf(line, "%u%c", &d, &c) == 2 && strchr(alphabet, c)) {                           /* the compact ones: steps since the last change and the letter */
            const unsigned v = (unsigned)(strchr(alphabet, c) - alphabet);
            at += d;
            rp_step[rp_n] = at;
            rp_keys[rp_n++] = ((v & 3) << 2) | (((v >> 2) & 3) << 4) | (((v >> 4) & 1) << 7);          /* left, right, A, B, Y as hw.h has them */
        }
    }
    fclose(f);
    fprintf(stderr, "replay: %d changes of the buttons\n", rp_n);
}

static uint32_t rp_keys_at(uint32_t step)
{
    uint32_t k = 0;
    for (int i = 0; i < rp_n && rp_step[i] <= step; i++) k = rp_keys[i];
    return k;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: racestats <image> <label> <out.csv> <steps> <every> [track]\n");
        return 2;
    }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    if (!track_open(TEX_FILE)) { fprintf(stderr, "not a track image (%zu bytes)\n", n); return 1; }
    if (!track_init()) { fprintf(stderr, "track_init: the image does not fit the limits\n"); return 1; }
    int loop = road_build();
    int tn = 1;
    for (const char *q = argv[1]; *q; q++) if (*q >= '1' && *q <= '7') tn = *q - '0';
    if (argc > 6) tn = atoi(argv[6]);
    int steps = atoi(argv[4]), every = atoi(argv[5]);
    if (every < 1) every = 1;
    if (getenv("TEAM") && getenv("CAR")) {              /* the menu's choice: the grid as s2menu_grid() has it (slot k: team (team + k) mod 4, car (car + k) mod 4 from 0) */
        const int team = atoi(getenv("TEAM")), car = atoi(getenv("CAR"));
        for (int k = 0; k < RACE_CARS; k++)
            race_grid[k] = (struct s2_pick){ (uint8_t)((team - 1 + k) % 4 + 1), (uint8_t)(k ? (car - 1 + k) % 4 + 1 : car) };
    }
    if (getenv("SKILL")) race_level = 2 - atoi(getenv("SKILL"));      /* 0 ROOKIE, 1 NORMAL, 2 PRO */
    if (getenv("LAPS")) race_laps = atoi(getenv("LAPS"));
    const int attract = getenv("ATTRACT") != 0;
    race_attract = attract;
    if (!race_init(tn, road_floor() - 3000.0f)) { fprintf(stderr, "race_init failed\n"); return 1; }
    struct s2car_state *p = race_car(0);
    float forever = 1e9f;
    const char *replay = getenv("REPLAY");
    const int fx_on = getenv("FX") != 0;
    if (replay) rp_load(replay);
    s2fx_reset();
    if (attract) {
        s2cam_begin(tn, S2CAM_TRACKSIDE);
    } else if (!replay) {
        p->rec[0x4c / 4] |= 1;                          /* the game's driver drives the player's car too */
        memcpy(&p->rec[1], &forever, 4);
    }
    FILE *out = fopen(argv[3], "w");
    if (!out) { perror(argv[3]); return 1; }
    FILE *pcs = 0;                                      /* PIECES=<file>: every piece of every frame (the pipeline model) */
    if (getenv("PIECES") && !(pcs = fopen(getenv("PIECES"), "w"))) { perror(getenv("PIECES")); return 1; }
    fprintf(out, "label,step,px,py,pz,node,mph,place,lap,c1x,c1y,c2x,c2y,c3x,c3y,ts_pieces,ts_faces,ts_triangles,ts_vertices,ts_objects,ts_nearest,yaw,fx_puffs");
    perf_header(out);
    fprintf(out, "\n");
    fprintf(stderr, "%s: road loop %d points, race of track %d, %d steps, a frame every %d\n", argv[2], loop, tn, steps, every);
    for (int k = 0; k < steps; k++) {
        struct s2car_input in = { .throttle = 0 };
        if (replay) {
            const uint32_t keys = rp_keys_at((uint32_t)k);
            in.throttle = (keys & KEY_B) ? 1.0f : 0.0f;
            in.brake = (keys & KEY_Y) ? 1.0f : 0.0f;
            in.steer = ((keys & KEY_RIGHT) ? 1.0f : 0.0f) - ((keys & KEY_LEFT) ? 1.0f : 0.0f);
            in.handbrake = (keys & KEY_A) != 0;
        }
        race_step(&in);
        if (fx_on) {                                    /* (the puffs of the step just taken, from the cars as they stand now: the viewer's order) */
            struct s2car_state *fc[RACE_CARS];
            for (int i = 0; i < race_cars(); i++) fc[i] = race_car(i);
            s2fx_update(fc, race_cars(), 1, race_steps() * 2, 0);
        }
        if (k % every) continue;
        static struct track_object objs[ROAD_CARS + S2FX_MAX];
        struct track_cam cam;
        struct s2car_state *rc[RACE_CARS];
        for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
        int nobj = road_race_scene(rc, race_cars(), &cam, objs);
        if (fx_on) nobj += s2fx_objects(&cam, rc, race_cars(), objs + nobj, ROAD_CARS + S2FX_MAX - nobj);
        if (attract) {
            s2cam_attract_tick(race_get_clock(), 0.02f * (float)every);
            s2cam_frame((const struct s2car_state *const *)rc, race_cars(), 0.02f * (float)every, &cam);
        }
        const struct rs_clip clip = { 0, 0, FB_W, FB_H };
        struct track_stats ts;
        memset(&ts, 0, sizeof ts);
        perf_reset();
        track_mode = TRACK_MODEL;
        track_draw_cam(FB(0), &clip, &cam, objs, nobj, &ts);
        float node;
        memcpy(&node, &p->rec[0x4b8 / 4], 4);
        struct race_hud h;
        race_get_hud(&h);
        fprintf(out, "%s,%d,%.0f,%.0f,%.0f,%.1f,%.0f,%d,%d", argv[2], k + 1, p->pos[0], p->pos[1], p->pos[2], node, p->speed_mph, h.place, h.lap);
        for (int i = 1; i < 4; i++) fprintf(out, ",%.0f,%.0f", race_car(i)->pos[0], race_car(i)->pos[1]);
        fprintf(out, ",%u,%u,%u,%u,%u,%u,%.2f,%d", ts.pieces, ts.faces, ts.triangles, ts.vertices, ts.objects, ts.nearest, p->yaw, s2fx_puffs());
        perf_row(out);
        fprintf(out, "\n");
        if (pcs) {
            fprintf(pcs, "F %d %d\n", k + 1, perf_npieces);
            for (int i = 0; i < perf_npieces; i++) {
                const struct perf_piece_rec *q = &perf_pieces[i];
                fprintf(pcs, "%u %u %u %u %u %u %u %u %u\n", q->kind, q->faces_row, q->gu_miss, q->refl_verts, q->verts, q->px_span,
                        q->tex_miss, q->walk_tris, q->rows);
            }
        }
    }
    fclose(out);
    if (pcs) fclose(pcs);
    return 0;
}
