/*
 * Report (not a check) on the painter's order of a computer car against the static pieces, run by tools/s2/report_carsort_s2.sh
 * on a track image. Not part of the firmware.
 *
 * The race robot (RACE_ROBOT: the game's driver drives the player) runs; from each of its cameras (every <kstep> steps of
 * <klo>..<khi>) a computer car is taken from 30, 50, ... 130 steps later (so it stands 4,000 to 25,000 units ahead, round the
 * next bends) and the frame is drawn with the firmware's own track.c. Every polygon the geometry unit hands on is also
 * rasterised into a depth map (the copies of geom.c and track.c that the script patches call dbg_poly() and set dbg_tag). A
 * pixel counts when the car is the last thing painted on it but a static piece's polygon there is nearer than the car by more
 * than 150 units (the car is drawn through it), the reverse when the car is covered although no static polygon is nearer.
 * Cases with at least 16 such pixels are printed, with the nearest piece that should have covered the car.
 * Usage: report_carsort_s2 <track image> <klo> <khi> <kstep>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "hw.h"
#include "track.c"
#include "blit.h"
#include "road.h"
#include "s2car.h"
#include "race.h"
#include "car.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))
#define MAXSTEP     2500
#define W           320
#define H           200
#define MARGIN      (150 * 256)         /* in the depth's 8 fraction bits */

static void map(void)
{
    int fd = memfd_create("sdram", 0);
    if(fd < 0 || ftruncate(fd, SDRAM_SIZE)
       || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
       || mmap((void *)(uintptr_t)SDRAM_UBASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
       || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        exit(1);
    }
}

static struct s2car_state hist[MAXSTEP][RACE_CARS];
static int paint_tag[H][W], nearest_tag[H][W];
static int32_t nearest_static[H][W], nearest_car[H][W];
int dbg_tag;                            /* set by the patched track.c before each piece or object is handed on */
static int car_tag;

static void tri(const struct su_point *a, const struct su_point *b, const struct su_point *c, const int32_t f[3], int tag)
{
    float x0 = (float)a->x / 16.0f, y0 = (float)a->y / 16.0f, x1 = (float)b->x / 16.0f, y1 = (float)b->y / 16.0f;
    float x2 = (float)c->x / 16.0f, y2 = (float)c->y / 16.0f;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if(fabsf(area) < 0.01f) return;
    int xa = (int)floorf(fminf(x0, fminf(x1, x2))), xb = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
    int ya = (int)floorf(fminf(y0, fminf(y1, y2))), yb = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    if(xa < 0) xa = 0;
    if(ya < 0) ya = 0;
    if(xb > W - 1) xb = W - 1;
    if(yb > H - 1) yb = H - 1;
    float i0 = 1.0f / (float)f[0], i1 = 1.0f / (float)f[1], i2 = 1.0f / (float)f[2];
    for(int y = ya; y <= yb; y++)
        for(int x = xa; x <= xb; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
            float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
            float w2 = 1.0f - w0 - w1;
            if(w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            int32_t d = (int32_t)(1.0f / (w0 * i0 + w1 * i1 + w2 * i2));
            paint_tag[y][x] = tag;
            if(tag == car_tag) { if(d < nearest_car[y][x]) nearest_car[y][x] = d; }
            else if(tag < car_tag - 1 && d < nearest_static[y][x]) { nearest_static[y][x] = d; nearest_tag[y][x] = tag; }   /* (a piece) */
        }
}

/* called by the patched geom.c with each polygon (view space, depth with 8 fraction bits) before the unit draws it */
void dbg_poly(const struct su_view *v, const struct su_fixed *c, int n)
{
    struct su_fixed out[SU_MAX_POLY];
    int m = su_clip(v, c, n, out);
    if(m < 3) return;
    struct su_point p[SU_MAX_POLY];
    for(int i = 0; i < m; i++) su_project(v, &out[i], &p[i]);
    for(int i = 2; i < m; i++) {
        int32_t f[3] = { out[0].f, out[i - 1].f, out[i].f };
        tri(&p[0], &p[i - 1], &p[i], f, dbg_tag);
    }
}

int main(int argc, char **argv)
{
    if(argc < 5) { fprintf(stderr, "usage: report_carsort_s2 <track image> <klo> <khi> <kstep>\n"); return 2; }
    map();
    FILE *fp = fopen(argv[1], "rb");
    if(!fp) { perror(argv[1]); return 2; }
    size_t n = fread(TEX_FILE, 1, 0x700000, fp);
    fclose(fp);
    if(!n || !track_open(TEX_FILE)) { fprintf(stderr, "%s: not a track image\n", argv[1]); return 2; }
    int np = track_init();
    if(!np || !road_build()) return 2;
    int tn = 1;
    for(const char *q = argv[1]; *q; q++) if(*q >= '1' && *q <= '7') tn = *q - '0';
    if(!race_init(tn, road_floor() - 3000.0f)) return 2;
    {                                           /* the game's driver drives the player's car too, as render_s2.c does */
        struct s2car_state *p = race_car(0);
        float forever = 1e9f;
        p->rec[0x4c / 4] |= 1;
        memcpy(&p->rec[1], &forever, 4);
    }
    int klo = atoi(argv[2]), khi = atoi(argv[3]), kst = atoi(argv[4]);
    int maxk = khi + 140 < MAXSTEP - 1 ? khi + 140 : MAXSTEP - 1;
    for(int k = 0; k <= maxk; k++) {
        struct s2car_input in = { .throttle = 0 };
        race_step(&in);
        for(int i = 0; i < RACE_CARS; i++) hist[k][i] = *race_car(i);
    }
    car_tag = np + 1;                           /* object 1 of the scene */
    int cases = 0, through = 0, covered = 0;
    for(int k = klo; k <= khi && k + 130 <= maxk; k += kst)
        for(int delta = 30; delta <= 130; delta += 20)
            for(int ci = 1; ci < RACE_CARS; ci++) {
                static struct s2car_state st[2];
                st[0] = hist[k][0];
                st[1] = hist[k + delta][ci];
                struct s2car_state *rc[2] = { &st[0], &st[1] };
                struct track_cam cam;
                static struct track_object objs[ROAD_CARS];
                int nobj = road_race_scene(rc, 2, &cam, objs);
                memset(paint_tag, 0xff, sizeof paint_tag);
                memset(nearest_tag, 0xff, sizeof nearest_tag);
                for(int y = 0; y < H; y++)
                    for(int x = 0; x < W; x++) nearest_static[y][x] = nearest_car[y][x] = 0x7fffffff;
                const struct rs_clip clip = { 0, 0, FB_W, FB_H };
                static struct blit_list l;
                blit_start(&l, 0);
                track_backdrop_blit(&l, &clip, &cam);
                blit_model(FB(0), &l);
                struct track_stats ts;
                memset(&ts, 0, sizeof ts);
                track_mode = TRACK_MODEL;
                track_draw_cam(FB(0), &clip, &cam, objs, nobj, &ts);
                int a = 0, b = 0, cov = 0, x0 = W, x1 = -1, y0 = H, y1 = -1, who = -1;
                for(int y = 0; y < H; y++)
                    for(int x = 0; x < W; x++) {
                        if(nearest_car[y][x] == 0x7fffffff) continue;
                        cov++;
                        if(paint_tag[y][x] == car_tag && nearest_static[y][x] < nearest_car[y][x] - MARGIN) {
                            a++;
                            who = nearest_tag[y][x];
                            if(x < x0) x0 = x;
                            if(x > x1) x1 = x;
                            if(y < y0) y0 = y;
                            if(y > y1) y1 = y;
                        } else if(paint_tag[y][x] >= 0 && paint_tag[y][x] < car_tag - 1 && nearest_static[y][x] > nearest_car[y][x] + MARGIN)
                            b++;
                    }
                cases++;
                if(a >= 16 && who < np) {
                    through++;
                    const struct s2car_state *s = &st[1];
                    float dx = s->pos[0] - cam.x, dy = s->pos[1] - cam.y, dh = s->pos[2] - cam.h;
                    const uint8_t *o, *blk = block_of(pieces[who].obj, &o);
                    printf("step %d, car %d from step %d: %d of %d car pixels drawn through a nearer piece (x %d..%d, y %d..%d): piece %d %.12s, bias %d; car at %.0f units, key %d\n",
                           k + 1, ci, k + 1 + delta, a, cov, x0, x1, y0, y1, who, (const char *)(blk + 0x68), -class_key[pieces[who].obj],
                           sqrtf(dx * dx + dy * dy + dh * dh), depth_key(dx * dx + dy * dy + dh * dh) + 300);
                }
                if(b >= 16) covered++;
            }
    printf("%d cases: the car is drawn through a nearer static piece in %d, covered by a farther one in %d\n", cases, through, covered);
    return 0;
}
