/*
 * EXPERIMENT (docs/screamer2/comparison.md, task 2): frames of a track image from the firmware's own
 * code (track.c, geom.c, setup.c, raster.c, blit.c; the C models of the units) on the PC, from
 * camera positions on the player's line or given freely. No cars, no race: only the track.
 * Built and run by tools/s2/render_s2.sh. Not part of the firmware.
 *
 * Usage: render_s2 <track image> <out prefix> <camera>...
 *   camera: p<i>    path point i (modulo the length), the camera over the line and looking ahead
 *           r<i>    road-centre point i of the map (MMP array 2) in the road's direction, over the
 *                   drivable surface there (track_under); S2's path (the opponents' line) leaves the road
 *           c<x>,<y>,<h>,<yaw>,<pitch>   free camera (yaw 0 = towards -y, radians)
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

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))      /* as main.c */

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

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: render_s2 <image> <prefix> <p<i>|c<x>,<y>,<h>,<yaw>,<pitch>>...\n"); return 2; }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    if (!track_open(TEX_FILE)) { fprintf(stderr, "track_open: not a track image (%zu bytes)\n", n); return 1; }
    int pieces = track_init();
    if (!pieces) { fprintf(stderr, "track_init: the image does not fit the limits (objects > MAX_OBJ or pieces > MAX_PIECES)\n"); return 1; }
    uint32_t np = track_path_length();
    printf("image %zu bytes, %d placements, path %u points\n", n, pieces, np);

    for (int a = 3; a < argc; a++) {
        struct track_cam cam;
        if (argv[a][0] == 'p') {
            uint32_t i = (uint32_t)atoi(argv[a] + 1) % np, il = (i + 12) % np;
            float x, y, h, x2, y2, h2;
            track_path_point(i, &x, &y, &h);
            track_path_point(il, &x2, &y2, &h2);
            cam.x = x; cam.y = y; cam.h = h + 700.0f;
            cam.yaw = atan2f(x2 - x, -(y2 - y));
            cam.pitch = atan2f(-(250.0f), hypotf(x2 - x, y2 - y));      /* a little down, as track_draw's */
            cam.roll = 0.0f;
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
        track_draw_cam(fb, &clip, &cam, 0, 0, &ts);
        char name[512];
        snprintf(name, sizeof name, "%s%d.rgb", argv[2], a - 3);
        write_rgb(name, track_palette());
        printf("camera %.0f %.0f %.0f yaw %.3f: %u pieces handed on, %u faces, %u triangles, %u vertices, %u see-through; %s\n",
               cam.x, cam.y, cam.h, cam.yaw, ts.pieces, ts.faces, ts.triangles, ts.vertices, ts.see_through, name);
    }
    return 0;
}
