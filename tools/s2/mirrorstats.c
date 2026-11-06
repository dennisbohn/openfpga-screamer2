/*
 * What the rear-view mirror's pass adds to a frame of the race (docs/screamer2/hud.md "The views from the car"): the race of firmware/race.c on the PC with the game's driver driving
 * the player's car too (as racestats.c), every `every` steps the viewer's scene (the chase camera, the four cars) drawn with the counters of perf_patch.py, then the mirror's pass
 * (track_draw_mirror: the camera behind the player's car, 320 x 35 rows, the other three cars, level of detail 1 at least) with the same counters. One CSV line per frame:
 * the pieces handed to the geometry unit (gu_pieces), its faces (faces_row), the polygons the setup unit gets (polys), the pixels the rasterizer draws (px_span in spans, px_drawn
 * written), the texture lines missed, for the main pass and for the mirror's. The shadows and the puffs are in neither (they are 1 to 5 % of a frame).
 * Built and run by tools/s2/mirrorstats.sh. Not part of the firmware.
 *
 * Usage: mirrorstats <track image> <label> <out.csv> <steps> <every> [track]
 * Needs the game's files for the physics: S2_GAME=<game folder> (mirrorstats.sh mounts it).
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
#include "s2cockpit.h"

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
        fprintf(stderr, "usage: mirrorstats <image> <label> <out.csv> <steps> <every> [track]\n");
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
    s2cam_begin(tn, S2CAM_BACK);
    FILE *out = fopen(argv[3], "w");
    if (!out) { perror(argv[3]); return 1; }
    fprintf(out, "label,step,node,mph,pieces,faces,polys,px_span,px_drawn,tex_lines,m_pieces,m_faces,m_polys,m_px_span,m_px_drawn,m_tex_lines\n");
    fprintf(stderr, "%s: road loop %d points, race of track %d, %d steps, a frame every %d\n", argv[2], loop, tn, steps, every);
    for (int k = 0; k < steps; k++) {
        struct s2car_input in = { .throttle = 0 };
        race_step(&in);
        if (k % every) continue;
        static struct track_object objs[ROAD_CARS], mobjs[ROAD_CARS];
        struct track_cam cam, mcam;
        struct s2car_state *rc[RACE_CARS];
        for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
        int nobj = road_race_scene(rc, race_cars(), &cam, objs);
        const struct rs_clip clip = { 0, 0, FB_W, FB_H }, mclip = { 0, 0, FB_W, S2CK_MIRROR_ROWS };
        struct track_stats ts, ms;
        memset(&ts, 0, sizeof ts);
        memset(&ms, 0, sizeof ms);
        track_mode = TRACK_MODEL;
        perf_reset();
        track_draw_cam(FB(0), &clip, &cam, objs, nobj, &ts);
        struct perf_counts a = perf_c;
        s2cam_mirror((const struct s2car_state *const *)rc, race_cars(), &mcam);
        int nm = 0;
        for (int i = 1; i < nobj; i++) mobjs[nm++] = objs[i];       /* (the player's car and shadow are not in the mirror) */
        perf_reset();
        track_draw_mirror(FB(0), &mclip, &mcam, mobjs, nm, &ms);
        struct perf_counts b = perf_c;
        float node;
        memcpy(&node, &p->rec[0x4b8 / 4], 4);
        fprintf(out, "%s,%d,%.1f,%.0f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", argv[2], k + 1, node, p->speed_mph, a.gu_pieces, a.faces_row, a.polys, a.px_span, a.px_drawn, a.tex_lines,
                b.gu_pieces, b.faces_row, b.polys, b.px_span, b.px_drawn, b.tex_lines);
    }
    fclose(out);
    return 0;
}
