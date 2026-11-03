/*
 * Frame statistics along a track's road, for Screamer 1 and Screamer 2 images alike
 * (docs/screamer2/performance.md). The firmware's own track.c, geom.c, setup.c and raster.c
 * (copies with counters, tools/s2/perf_patch.py) draw frames from the near chase camera over
 * road points; every frame's counters go into a CSV line, once per number of cars.
 * Built and run by tools/s2/framestats.sh. Not part of the firmware.
 *
 * Usage: framestats <track image> <label> <out.csv> <step> <cars> [grid]
 *   step   every step-th road point (MMP array 2) is a frame; -N: about N frames
 *   cars   comma list of car counts: 0 no car at all, 1 the player's car (always in the chase
 *          view), n the player's and n - 1 opponents ahead (see frame()); models taken in turn
 *          from the image (Screamer 1: AUTO00..06, Screamer 2: AUTO1..5 of a team's cars file)
 *   grid   (optional word) one more frame: the start grid, player at the start place
 * MIRROR=1 in the environment: with every road frame the rear-view mirror's pass too (kind
 * "mirror", cars = opponents behind the player).
 * FAR=n [REACH=m] in the environment: the far plane (faces) and the reach (placements) in units,
 * default as the firmware has them for HI detail (43808, 52000); Screamer 2: TRACK_DATA's far
 * plane and 1.2 times it (OpenScreamer's rule for the reach).
 * LOD=n in the environment (Screamer 2 build): the original's level of detail per block, 1 = placed meshes,
 * 2 = cars, 3 = both (perf.h); 0 or unset = every block at level 0 as the firmware draws them.
 * The camera is the game's near chase camera (car.c, views[2]): 1300 behind, 300 above, looking
 * down 0.1885 rad, over a car standing on the road at that point.
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
#include "perf.h"

void perf_set_far(float far, float reach);      /* track.c's copy (perf_patch.py) */

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))      /* as main.c */
#define MAX_CARS    10

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

static const struct track_dir *dirs;
static int nd;

/* the road a distance ahead of road point i: place and heading (the points run the way the road
   is driven when the next one lies in the heading's direction, else the other way) */
static void road_ahead(int i, float dist, float *x, float *y, float *yaw)
{
    const struct track_dir *q = &dirs[i % nd], *n = &dirs[(i + 1) % nd];
    float fx = sinf(q->angle), fy = -cosf(q->angle);
    int dir = ((float)(n->x - q->x) * fx + (float)(n->y - q->y) * fy) >= 0.0f ? 1 : -1;
    if (dist < 0.0f) { dir = -dir; dist = -dist; }      /* behind */
    float acc = 0.0f;
    int k = i;
    for (int guard = 0; guard < nd; guard++) {
        const struct track_dir *a = &dirs[(k % nd + nd) % nd], *b = &dirs[((k + dir) % nd + nd) % nd];
        float seg = hypotf((float)(b->x - a->x), (float)(b->y - a->y));
        if (acc + seg >= dist) {
            float t = seg > 0.0f ? (dist - acc) / seg : 0.0f;
            *x = (float)a->x + t * (float)(b->x - a->x);
            *y = (float)a->y + t * (float)(b->y - a->y);
            *yaw = a->angle;
            return;
        }
        acc += seg;
        k += dir;
    }
    *x = (float)q->x;
    *y = (float)q->y;
    *yaw = q->angle;
}

static float ground(float x, float y, float fallback)
{
    float h = fallback;
    int grass;
    return track_under(x, y, &h, &grass) ? h : fallback;
}

/* the near chase camera over a car standing at (x, y, z) heading yaw */
static struct track_cam chase_cam(float x, float y, float z, float yaw)
{
    const float behind = 1300.0f, above = 300.0f, down = 0.1885f;
    float pitch = -down;
    struct track_cam c;
    c.x = x - cosf(pitch) * behind * sinf(yaw);
    c.y = y + cosf(pitch) * behind * cosf(yaw);
    c.h = z + above - behind * sinf(pitch);
    c.yaw = yaw;
    c.pitch = pitch;
    c.roll = 0.0f;
    return c;
}

static void add_car(struct track_object *objs, int *nobj, struct track_block (*blocks)[CAR_BLOCKS], int model,
                    float x, float y, float z, float yaw, const struct track_cam *cam)
{
    int nm;
    track_models(&nm);
    struct car_look lk = { x, y, z, yaw, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0x4000u >> 2 };
    struct track_object o;
    memset(&o, 0, sizeof o);
    if (car_draw(model % (nm ? nm : 1), &lk, cam, blocks[*nobj], &o)) objs[(*nobj)++] = o;
}

/* one frame: the player's car at (x, y, z, yaw) if ncars >= 1, opponents ahead */
static void frame(FILE *out, const char *label, int idx, float x, float y, float z, float yaw, int ncars,
                  int grid, int mirror, const char *tag)
{
    static struct track_object objs[MAX_CARS + 2];
    static struct track_block blocks[MAX_CARS + 2][CAR_BLOCKS];
    int nobj = 0, nm;
    perf_reset();
    track_models(&nm);
    perf_lod_min = mirror;                      /* the mirror's pass takes level 1 at least (STARTL.EXE 0x7f0fc) */
    struct track_cam cam = chase_cam(x, y, z, yaw);
    /* the rear-view mirror (NOTES2 S2-5zh): the scene again from the car's place, 400 above the
       body, looking back, 35 rows of 320, without the car; the others are behind it */
    if (mirror) cam = (struct track_cam){ x, y, z + 400.0f, yaw + 3.14159265f, 0.0f, 0.0f };
    if (ncars >= 1 && !mirror) add_car(objs, &nobj, blocks, 0, x, y, z, yaw, &cam);
    /* the start grid of Screamer 1 (track 1, measured in the game): nine opponents in rows 4,000
       apart, two columns 3,600 apart, the player last in the right column; (ahead, left) in units.
       The first is beside the player. On the road: opponents 2,200 ahead and then every 1,700,
       alternately 500 left and right of the middle. */
    static const float gridpos[9][2] = { { 0, 3398 }, { 4117, -200 }, { 4143, 3597 }, { 8217, -199 }, { 8186, 3687 },
                                         { 12378, -89 }, { 12357, 3694 }, { 15751, -131 }, { 15720, 3655 } };
    for (int k = 1; k < ncars && k < MAX_CARS; k++) {
        float ahead = grid ? gridpos[k - 1][0] : 2200.0f + 1700.0f * (float)(k - 1);
        if (mirror) ahead = -(1500.0f + 1700.0f * (float)(k - 1));
        float side = grid ? gridpos[k - 1][1] : (k & 1) ? 500.0f : -500.0f, ox, oy, oyaw;
        if (grid) {
            ox = x + sinf(yaw) * ahead + cosf(yaw) * side;
            oy = y - cosf(yaw) * ahead + sinf(yaw) * side;
            oyaw = yaw;
        } else {
            float rx, ry;
            road_ahead(idx, ahead, &rx, &ry, &oyaw);
            ox = rx + cosf(oyaw) * side;
            oy = ry + sinf(oyaw) * side;
        }
        add_car(objs, &nobj, blocks, k % (nm ? nm : 1), ox, oy, ground(ox, oy, z), oyaw, &cam);
    }
    const struct rs_clip clip = { 0, 0, FB_W, mirror ? 35 : FB_H };
    struct track_stats ts;
    memset(&ts, 0, sizeof ts);
    uint32_t c1 = perf_c.lod_c1, c2 = perf_c.lod_c2, cd = perf_c.lod_cd;      /* counted when the cars were made */
    perf_reset();
    perf_c.lod_c1 = c1; perf_c.lod_c2 = c2; perf_c.lod_cd = cd;
    track_mode = TRACK_MODEL;
    track_draw_cam(FB(0), &clip, &cam, objs, nobj, &ts);
    fprintf(out, "%s,%s,%d,%.0f,%.0f,%.0f,%d,%u,%u,%u,%u,%u,%u", label, tag, idx, x, y, z, ncars, ts.pieces, ts.faces,
            ts.triangles, ts.vertices, ts.objects, ts.nearest);
    perf_row(out);
    fprintf(out, "\n");
}

/* the blocks of every car model of the image: name, flags, vertices, faces in a few face rows (the
   rows differ by the side the eye is on), to see what a car costs and which blocks are tyres and hubs */
static void carinfo(void)
{
    int nm;
    track_models(&nm);
    for (int k = 0; k < nm; k++) {
        const uint8_t *m = track_model_mesh(k);
        int nb = *(const int32_t *)(m + 0x14);
        int tv = 0, tf = 0, tr = 0, th = 0, nrm = 0;
        printf("model %d: %d blocks\n", k, nb);
        for (int b = 0; b < nb; b++) {
            const uint8_t *blk = m + *(const int32_t *)(m + 0x18 + 4 * b);
            int nv = *(const int32_t *)(blk + 0x10), flags = *(const int32_t *)(blk + 0x18);
            int nf[16];
            for (int r = 0; r < 16; r++) {
                int ro = *(const int32_t *)(blk + 0x28 + 4 * r), n = 0;
                if (ro > 0) while (*(const uint16_t *)(blk + ro + 2 * n) != 0xFFFF && n < 1024) n++;
                nf[r] = n;
            }
            char name[5] = { 0 };
            memcpy(name, blk + 0x68, 4);
            int sum = 0;
            for (int r = 0; r < 16; r++) sum += nf[r];
            printf("  %-4s flags %05x vertices %3d faces in row 0/4/8/12: %2d %2d %2d %2d (mean %.0f)\n", name, flags, nv, nf[0], nf[4], nf[8], nf[12], sum / 16.0);
            tv += nv;
            tf += sum / 16;
            int hub = name[2] == 'x' && name[0] != 'A' && name[0] != 'a', tyre = name[2] == 'i' && name[0] != 'A' && name[0] != 'a';
            th += hub ? sum / 16 : 0;
            tr += tyre ? sum / 16 : 0;
            nrm += (flags & 0x2000) ? nv : 0;
        }
        printf("  total: %d vertices, %d faces in a row (mean), of which tyres %d and hubs %d; %d vertices with normals\n", tv, tf, tr, th, nrm);
    }
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[2], "carinfo")) {
        map();
        FILE *fi = fopen(argv[1], "rb");
        if (!fi || !fread(TEX_FILE, 1, 0x700000, fi) || !track_open(TEX_FILE) || !track_init()) { fprintf(stderr, "image?\n"); return 1; }
        fclose(fi);
        carinfo();
        return 0;
    }
    if (argc < 6) {
        fprintf(stderr, "usage: framestats <image> <label> <out.csv> <step> <cars,...> [grid]\n");
        return 2;
    }
    map();
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    size_t n = fread(TEX_FILE, 1, 0x700000, f);
    fclose(f);
    if (!track_open(TEX_FILE)) { fprintf(stderr, "not a track image (%zu bytes)\n", n); return 1; }
    int placements = track_init();
    if (!placements) { fprintf(stderr, "track_init: the image does not fit the limits\n"); return 1; }
    dirs = track_dirs(&nd);
    if (getenv("FAR")) {                        /* FAR=50000 [REACH=60000]: the track's view distances */
        float far = (float)atof(getenv("FAR"));
        perf_set_far(far, getenv("REACH") ? (float)atof(getenv("REACH")) : 1.2f * far);
        fprintf(stderr, "far plane %.0f, reach %.0f\n", far, getenv("REACH") ? atof(getenv("REACH")) : 1.2 * far);
    }
    int step = atoi(argv[4]);
    if (step < 0) step = nd / -step;            /* -N: about N frames */
    if (step < 1) step = 1;
    int cars[8], ncv = 0;
    for (char *s = strtok(argv[5], ","); s && ncv < 8; s = strtok(0, ",")) cars[ncv++] = atoi(s);

    FILE *out = fopen(argv[3], "w");
    if (!out) { perror(argv[3]); return 1; }
    fprintf(out, "label,kind,idx,x,y,z,ncars,ts_pieces,ts_faces,ts_triangles,ts_vertices,ts_objects,ts_nearest");
    perf_header(out);
    fprintf(out, "\n");
    fprintf(stderr, "%s: %d placements, %d road points\n", argv[2], placements, nd);

    /* the road's height at every point; where track_under finds nothing (the middle of the road is
       not always over a drivable piece), that of the nearest point that has one */
    float *zr = malloc(sizeof(float) * (size_t)nd);
    char *ok = malloc((size_t)nd);
    int bad = 0;
    for (int i = 0; i < nd; i++) {
        float h = 0.0f;
        int grass;
        ok[i] = (char)track_under((float)dirs[i].x, (float)dirs[i].y, &h, &grass);
        zr[i] = h;
        bad += !ok[i];
    }
    for (int i = 0; i < nd; i++)
        for (int d = 1; !ok[i] && d < nd; d++) {
            int a = (i + d) % nd, b = (i - d + nd) % nd;
            if (ok[a]) { zr[i] = zr[a]; break; }
            if (ok[b]) { zr[i] = zr[b]; break; }
        }
    fprintf(stderr, "%d of %d road points have no drivable piece under them (height of the nearest one)\n", bad, nd);

    for (int i = 0; i < nd; i += step) {
        const struct track_dir *q = &dirs[i];
        for (int v = 0; v < ncv; v++) frame(out, argv[2], i, (float)q->x, (float)q->y, zr[i], q->angle, cars[v], 0, 0, "road");
        if (getenv("MIRROR"))
            for (int v = 0; v < ncv; v++) frame(out, argv[2], i, (float)q->x, (float)q->y, zr[i], q->angle, cars[v], 0, 1, "mirror");
    }
    if (argc > 6 && !strcmp(argv[6], "grid")) {
        float x, y, yaw;
        track_start(&x, &y, &yaw);
        int near = 0;
        float bd = 1e30f;
        for (int i = 0; i < nd; i++) {
            float d = hypotf((float)dirs[i].x - x, (float)dirs[i].y - y);
            if (d < bd) { bd = d; near = i; }
        }
        float z = ground(x, y, zr[near]);
        fprintf(stderr, "start %.0f %.0f %.0f heading %.3f\n", x, y, z, yaw);
        for (int v = 0; v < ncv; v++) frame(out, argv[2], -1, x, y, z, yaw, cars[v], 1, 0, "grid");
    }
    fclose(out);
    return 0;
}
