/*
 * Host test of the track's culling and class keys (firmware/track.c, #included so that its statics can be read), run by
 * tools/s2/check_vis_s2.sh on a track image. Not part of the firmware.
 *
 *  1. The class key of every mesh (the object header's table entry 43, +0xC4, 0x7e9a5): track_class_key() equals minus
 *     the 16-bit word read from the file bytes; the meshes with a bias are counted.
 *  2. The walk over the map (docs/screamer2/draw_distance.md): from cameras round the road (every road point, 7 places across,
 *     3 headings, 1,300 units behind the point), every piece that the whole-window walk takes (all cells, within reach,
 *     in view: the cull's own tests) within NEAR_UNITS of the camera must also be taken by the walk over the camera cell's
 *     list. NEAR_UNITS is the range of the placed meshes' full detail (6,000: docs/screamer2/performance.md section 6),
 *     where a wall piece dropped for want of a list entry would show as a gap at the road's edge. The same count is
 *     printed for 9,000 and 12,000 units as information (the lists do drop pieces further out: that is the original's own).
 * Usage: check_vis_s2 <track image>      exit code 1 on a failure
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "hw.h"
#include "track.c"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))
#define NEAR_UNITS  6000.0f

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

/* the cull's tests of draw() for a piece (without the cell's box and the list): 1 if the piece is taken */
static int taken(int i, const float c[3], const float f[3], const float r[3], const float u[3], float *dist)
{
    const struct cpiece *pc = &cpieces[i];
    const float far_eye = far_z + EYE_BACK, half_w = 160.0f / FOCAL;
    const int eye = EYE_LINE(200);
    const float up = (float)eye / FOCAL, down = (float)(200 - eye) / FOCAL;
    float rad = pc->rad, dx = pc->x - c[0], dy = pc->y - c[1], dh = pc->h - c[2];
    float d2 = dx * dx + dy * dy + dh * dh;
    float vf = dx * f[0] + dy * f[1] + dh * f[2] + EYE_BACK, vr = dx * r[0] + dy * r[1] + dh * r[2];
    float vu = dx * u[0] + dy * u[1] + dh * u[2];
    *dist = sqrtf(d2);
    if(rad < 0.0f || d2 >= REACH2) return 0;
    if(vf + rad < NEAR || vf - rad > far_eye) return 0;
    if(vr - rad > half_w * (vf + rad) || vr + rad < -half_w * (vf + rad)) return 0;
    if(vu - rad > up * (vf + rad) || vu + rad < -down * (vf + rad)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if(argc < 2) { fprintf(stderr, "usage: check_vis_s2 <track image>\n"); return 2; }
    map();
    FILE *fp = fopen(argv[1], "rb");
    if(!fp) { perror(argv[1]); return 2; }
    size_t n = fread(TEX_FILE, 1, 0x700000, fp);
    fclose(fp);
    if(!n || !track_open(TEX_FILE)) { fprintf(stderr, "%s: not a track image\n", argv[1]); return 2; }
    int np = track_init();
    if(!np) { fprintf(stderr, "track_init failed\n"); return 2; }
    int bad = 0;

    /* 1. the class keys */
    int biased = 0, minb = 0, maxb = 0, wrong = 0;
    for(int k = 0; k < unr_count; k++) {
        const uint8_t *o;
        block_of(k, &o);
        int b = (int16_t)(uint16_t)(o[0xC4] | o[0xC5] << 8);
        if(track_class_key(o) != -b) wrong++;
        if(b) biased++;
        if(b < minb) minb = b;
        if(b > maxb) maxb = b;
    }
    printf("class keys: %d meshes, %d with a bias (%d .. %d)%s\n", unr_count, biased, minb, maxb, wrong ? " FAIL" : "");
    if(wrong) bad++;

    /* 2. the walks */
    static int piece_cell[MAX_PIECES];
    for(int g = 0; g < ncells; g++)
        for(int i = cells[g].first; i < cells[g].first + cells[g].count; i++) piece_cell[i] = g;
    int nd;
    const struct track_dir *dirs = track_dirs(&nd);
    long cams = 0, with_list = 0, lost[3] = { 0, 0, 0 };
    int worst = 0;
    const float limit[3] = { NEAR_UNITS, 9000.0f, 12000.0f };
    for(int d = 0; d < nd; d++) {
        float h = 0.0f;
        int grass;
        if(!track_under((float)dirs[d].x, (float)dirs[d].y, &h, &grass)) continue;
        for(int lat = -3; lat <= 3; lat++)
            for(int dy = -1; dy <= 1; dy++) {
                float yaw = dirs[d].angle + 0.35f * (float)dy, fx = sinf(yaw), fy = -cosf(yaw);
                struct track_cam cam = { dirs[d].x - fx * 1300.0f - fy * 700.0f * (float)lat, dirs[d].y - fy * 1300.0f + fx * 700.0f * (float)lat,
                                         h + 500.0f, yaw, -0.2f, 0.0f };
                float c[3] = { cam.x, cam.y, cam.h }, f[3], r[3], u[3];
                cam_frame(&cam, f, r, u);
                int cc = camera_cell(c[0], c[1]);
                cams++;
                if(cc < 0 || !vcells[cc].nlist) continue;
                with_list++;
                static uint8_t inlist[MAX_CELLS];
                memset(inlist, 0, sizeof inlist);
                for(int q = 0; q < vcells[cc].nlist; q++) inlist[vlist[vcells[cc].list + q]] = 1;
                int hit[3] = { 0, 0, 0 };
                for(int i = 0; i < np; i++) {
                    float dist;
                    if(inlist[piece_cell[i]] || !taken(i, c, f, r, u, &dist)) continue;
                    for(int k = 0; k < 3; k++) if(dist <= limit[k]) hit[k]++;
                }
                for(int k = 0; k < 3; k++) lost[k] += hit[k] > 0;
                if(hit[0] > worst) worst = hit[0];
            }
    }
    printf("walks: %ld cameras round the road, %ld with a list; a piece the whole walk takes is left out by the list within "
           "%.0f units in %ld of them%s, within 9000 in %ld, within 12000 in %ld\n", cams, with_list, NEAR_UNITS, lost[0],
           lost[0] ? " FAIL" : "", lost[1], lost[2]);
    if(lost[0]) bad++;
    return bad ? 1 : 0;
}
