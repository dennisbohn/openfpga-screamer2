/*
 * Frame statistics along a race (docs/screamer2/performance.md, section 7): the race of firmware/race.c on
 * the PC with the game's driver also driving the player's car (the "race robot" of render_s2.c), the scene
 * of the viewer's race (road_race_scene: the physics' chase camera, the four cars where they are) drawn
 * every `every` steps with the counters of perf_patch.py; one CSV line per frame, as framestats.c has it
 * plus the race (step, the player's node on the line, speed, the cars' places).
 * Built and run by tools/s2/racestats.sh. Not part of the firmware.
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
    if (!race_init(tn, road_floor() - 3000.0f)) { fprintf(stderr, "race_init failed\n"); return 1; }
    struct s2car_state *p = race_car(0);
    float forever = 1e9f;
    p->rec[0x4c / 4] |= 1;                              /* the game's driver drives the player's car too */
    memcpy(&p->rec[1], &forever, 4);
    FILE *out = fopen(argv[3], "w");
    if (!out) { perror(argv[3]); return 1; }
    FILE *pcs = 0;                                      /* PIECES=<file>: every piece of every frame (the pipeline model) */
    if (getenv("PIECES") && !(pcs = fopen(getenv("PIECES"), "w"))) { perror(getenv("PIECES")); return 1; }
    fprintf(out, "label,step,px,py,pz,node,mph,place,lap,c1x,c1y,c2x,c2y,c3x,c3y,ts_pieces,ts_faces,ts_triangles,ts_vertices,ts_objects,ts_nearest");
    perf_header(out);
    fprintf(out, "\n");
    fprintf(stderr, "%s: road loop %d points, race of track %d, %d steps, a frame every %d\n", argv[2], loop, tn, steps, every);
    for (int k = 0; k < steps; k++) {
        struct s2car_input in = { .throttle = 0 };
        race_step(&in);
        if (k % every) continue;
        static struct track_object objs[ROAD_CARS];
        struct track_cam cam;
        struct s2car_state *rc[RACE_CARS];
        for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
        int nobj = road_race_scene(rc, race_cars(), &cam, objs);
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
        fprintf(out, ",%u,%u,%u,%u,%u,%u", ts.pieces, ts.faces, ts.triangles, ts.vertices, ts.objects, ts.nearest);
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
