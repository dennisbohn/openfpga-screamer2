/*
 * The wheel smoke and the shadows (firmware/s2fx.c) against the original's memory: the recording tools/s2/dosbox_smoke_s2.py makes of the running game (a frame: the race
 * clock, the 100 Hz counter, the raw frame time, the four cars' whole records, the objects' table, the camera record, then the shadows' and the puffs' object records).
 * Built and run by tools/s2/check_fx_s2.sh.
 *
 *   check_fx_s2 check <image> <track> <recording> <frame bytes> <objects in the table>
 *       the car records of every frame go to s2car_fx() and s2fx_update() (the effects read nothing else), and after each frame the puffs the firmware has are the game's:
 *       which of the 4 x 16 records are alive; a puff born in the frame sits where the game's rule puts it (the rear corner's place, or the wheel's own place in the car for
 *       the camera's car, plus ((2 d & T) - d) x (x & 7) and x (z & 7) for a T the counter had while the frame was worked: the five values of the sample and the four
 *       before), the firmware's spread being one of them; then the place the firmware puts the puff at (the game's rec +0x18 .. put in, so that the spread's own chance does
 *       not run through the frames) is the game's: world places as they stand, carried ones the car's place plus the game's rotated spot (rec +0xc ..); a puff lives 30 frames
 *   check_fx_s2 render <image> <track> <recording> <frame bytes> <objects> <first> <last> <out prefix>
 *       the same replayed, and from frame <first> to <last> the scene (the cars as the game's records pose them, their shadows and puffs) drawn by the firmware's engine from
 *       the camera record of the frame into <out prefix><frame>.rgb (320 x 200 x 3); with NOFX=1 without the shadows and the puffs, with FXONLY=shadow or puff only those; with COST=1 (no sanitizer) check also counts the pixels and faces the effects' pieces draw
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
#include "s2fx.h"
#include "raster.h"

#define TEX_FILE    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))
#define REC         0xCC0
#define CARS        4
#define POOL(k)     ((k) ? 0x368 : 0x228)

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

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*n);
    if (fread(p, 1, *n, f) != *n) { perror(path); exit(2); }
    fclose(f);
    return p;
}

static int32_t i32(const uint8_t *p, uint32_t o) { int32_t v; memcpy(&v, p + o, 4); return v; }
static float f32(const uint8_t *p, uint32_t o) { float v; memcpy(&v, p + o, 4); return v; }

/* a car's record as the firmware's s2car_state: the game's words, but for the place (+0x4d8, +0x4e0), which the game keeps as whole numbers and the firmware as floats; the
   outputs the drawing and the effects read are made from the record as s2car.c's steps make them */
static void state_of(struct s2car_state *s, const uint8_t *rec)
{
    memset(s, 0, sizeof *s);
    memcpy(s->rec, rec, REC);
    float x = (float)i32(rec, 0x4d8), z = (float)i32(rec, 0x4e0);
    memcpy((uint8_t *)s->rec + 0x4d8, &x, 4);
    memcpy((uint8_t *)s->rec + 0x4e0, &z, 4);
    s->pos[0] = x;
    s->pos[1] = z;
    s->pos[2] = (float)i32(rec, 0x4dc);
    s->pitch = f32(rec, 0x4e4);
    s->yaw = f32(rec, 0x4e8);
    s->roll = f32(rec, 0x4ec);
    s->body_pitch = f32(rec, 0xC18);
    s->body_roll = f32(rec, 0xC20);
    s->wheel_steer = f32(rec, 0x13C);
    s->wheel_spin[0] = f32(rec, 0x584);
    s->look = (uint32_t)i32(rec, 0xB8) | (f32(rec, 0x1E8) + f32(rec, 0x1E4) != 0.0f ? 0x200u : 0u);
}

static struct { int n; const uint8_t *d; size_t frame; int nobj; } rec;

static const uint8_t *frame_at(int i) { return rec.d + rec.frame * (size_t)i; }

/* the camera record of a frame as a track_cam (s2cam.c's: x, h, z whole numbers at +0x48, pitch, yaw, roll at +0x54) */
static struct track_cam camera_of(const uint8_t *fr)
{
    const uint8_t *c = fr + 12 + CARS * REC + 0x1C * rec.nobj;
    return (struct track_cam){ (float)i32(c, 0x48), (float)i32(c, 0x50), (float)i32(c, 0x4C), f32(c, 0x58), f32(c, 0x54), f32(c, 0x5C) };
}

static int bad;
static uint32_t fx_pieces, fx_px_sum, fx_px_max, fx_faces_sum, fx_faces_max;
static int fx_px_max_at;

/* the scene of a frame: the cars as the records pose them, then the effects, from the recorded camera, into <prefix><frame>.rgb */
static void render_frame(int fi, const uint8_t *fr, struct s2car_state **cp, const char *prefix)
{
    struct track_cam cam = camera_of(fr);
    static struct track_object objs[CARS + S2FX_MAX];
    int no = road_race_scene(cp, CARS, &cam, objs);
    cam = camera_of(fr);
    if (!getenv("NOFX")) {
        static struct track_object fxo[S2FX_MAX];
        const int nf = s2fx_objects(&cam, cp, CARS, fxo, S2FX_MAX);
        const char *only = getenv("FXONLY");                  /* shadow: only the shadows (the objects with the quad's key), puff: only the puffs */
        for (int i = 0; i < nf; i++)
            if (!only || (!strcmp(only, "shadow")) == (fxo[i].key != 0)) objs[no++] = fxo[i];
    }
    const struct rs_clip clip = { 0, 0, FB_W, FB_H };
    volatile uint8_t *fb = FB(0);
    static struct blit_list l;
    blit_start(&l, 0);
    track_backdrop_blit(&l, &clip, &cam);
    blit_model(fb, &l);
    struct track_stats ts;
    track_mode = TRACK_MODEL;
    track_draw_cam(fb, &clip, &cam, objs, no, &ts);
    char path[512];
    snprintf(path, sizeof path, "%s%d.rgb", prefix, fi);
    FILE *f = fopen(path, "wb");
    const uint32_t *pal = track_palette();
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(pal[fb[i]] >> 16), (uint8_t)(pal[fb[i]] >> 8), (uint8_t)pal[fb[i]] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("frame %d: %d objects, %u pieces, %u faces, %u of them see-through\n", fi, no, ts.pieces, ts.faces, ts.see_through);
}
#define FAIL(...) do { if (++bad <= 12) { printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage: check_fx_s2 check|render <image> <track> <recording> <frame bytes> <objects> [first last prefix]\n"); return 2; }
    const int render = !strcmp(argv[1], "render");
    map();
    size_t n;
    uint8_t *img = slurp(argv[2], &n);
    memcpy(TEX_FILE, img, n);
    if (!track_open(TEX_FILE) || !track_init()) { fprintf(stderr, "the image does not open\n"); return 2; }
    const int track = atoi(argv[3]);
    size_t rn;
    rec.d = slurp(argv[4], &rn);
    rec.frame = (size_t)atoi(argv[5]);
    rec.nobj = atoi(argv[6]);
    rec.n = (int)(rn / rec.frame);
    struct track_fx fx;
    if (!track_fx(&fx)) { fprintf(stderr, "the image has no effects block\n"); return 2; }
    rs_smg = fx.table;
    struct s2car_state boot;
    if (!s2car_init(&boot, 0, 0, track, 0)) { fprintf(stderr, "the car's files (S2_GAME) are not there\n"); return 2; }     /* (loads the track's road kinds) */

    static struct s2car_state cars[CARS];
    struct s2car_state *cp[CARS];
    for (int i = 0; i < CARS; i++) cp[i] = &cars[i];
    const int shift = getenv("SHIFT") ? atoi(getenv("SHIFT")) : 0;
    const int first = render ? atoi(argv[7]) : 0, last = render ? atoi(argv[8]) : rec.n - 1;
    const int has_objs = rec.frame == 12 + 4 * REC + 0x1C * (size_t)rec.nobj + 0xAEC + 4 * 17 * 0x40;     /* (the first recordings have no object records) */
    int places_unit = 0, silent = 0, shadows = 0, shadows_ok = 0, shadows_exact = 0, doubles = 0, ages = 0, ages_ok = 0, frames_ok = 0, places = 0, places_ok = 0, carried_n = 0, carried_ok = 0, spawns = 0, spawns_ok = 0, ended = 0, ended_30 = 0, wrong_on = 0;
    int born[CARS][2][S2FX_PUFFS], alive_prev[CARS][2][S2FX_PUFFS];
    memset(born, 0, sizeof born);
    memset(alive_prev, 0, sizeof alive_prev);
    s2fx_reset();
    for (int fi = 0; fi <= last; fi++) {
        const uint8_t *fr = frame_at(fi), *pf = frame_at(fi ? fi - 1 : 0);
        const uint32_t T = (uint32_t)i32(fr, 4);
        const uint8_t *cf = frame_at(fi + shift < rec.n ? fi + shift : rec.n - 1);      /* (SHIFT=n: the cars n frames ahead of the puffs: the negative control, which must fail) */
        for (int i = 0; i < CARS; i++) state_of(&cars[i], cf + 12 + REC * i);
        s2fx_update(cp, CARS, 1, T - 2, 0);
        /* a frame in which the game worked a step twice (cars touching: its solver runs the step again; the car's timer shows it) is not a step of ours: the car's puffs and
           timer are taken from the game's records from there on */
        int resync[CARS] = { 0 };
        for (int i = 0; i < CARS; i++) {
            const float t1 = f32(fr, 12 + REC * i + 0x224);
            if (fabsf(s2fx_timer(i, 0, 0.0f) - t1) > 0.001f) {
                resync[i] = 1;
                doubles++;
                s2fx_timer(i, 1, t1);
            }
        }
        for (int i = 0; i < CARS; i++) {                      /* the same where the game's puffs aged by a step more than a frame of ours has: its solver stopped a step short and ran it again */
            for (int k = 0; k < 2 && !resync[i]; k++)
                for (int j = 0; j < S2FX_PUFFS; j++) {
                    float p[3], age;
                    int carried;
                    if ((i32(fr, 12 + REC * i + POOL(k) + 0x28 * j + 8) != 0) != s2fx_puff_state(i, k, j, p, &age, &carried)) {
                        resync[i] = 1;
                        silent++;
                        break;
                    }
                }
        }
        int alive_game = 0;
        for (int i = 0; i < CARS; i++) {
            const uint8_t *r = fr + 12 + REC * i, *pr = pf + 12 + REC * i;
            struct s2car_state pstate;
            state_of(&pstate, pr);
            struct s2car_fx before;
            s2car_fx(&pstate, &before);                            /* the car as the frame before left it: what the spawn saw */
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < S2FX_PUFFS; j++) {
                    const uint8_t *q = r + POOL(k) + 0x28 * j;
                    const int on = i32(q, 8) != 0;
                    float p[3], age;
                    int carried;
                    const int ours = s2fx_puff_state(i, k, j, p, &age, &carried);
                    alive_game += on;
                    if (resync[i]) {
                        const size_t oo = 12 + 4 * REC + 0x1C * (size_t)rec.nobj + 0xAEC + (size_t)(i * 17 + 1 + k * 8 + j) * 0x40;
                        const float gp[3] = { (float)i32(q, 0x18), (float)i32(q, 0x1C), (float)i32(q, 0x20) };
                        s2fx_puff_load(i, k, j, on, has_objs ? (on ? (float)i32(fr, (uint32_t)oo + 0x20) / 65536.0f : 0.0f) : (ours ? age : 0.0f), gp, ours ? carried : (i == 0));
                        alive_prev[i][k][j] = on;
                        continue;
                    }
                    if (on != ours) { wrong_on++; FAIL("frame %d car %d kind %d record %d: the game has it %s, the firmware %s", fi, i, k, j, on ? "alive" : "not", ours ? "alive" : "not"); }
                    if (on && !ours) continue;
                    if (on && !alive_prev[i][k][j]) {              /* born in this frame */
                        spawns++;
                        born[i][k][j] = fi;
                        const int32_t gx = i32(q, 0x18), gz = i32(q, 0x20);
                        const int32_t X7 = i32(pr, 0x4d8) & 7, Z7 = i32(pr, 0x4e0) & 7;
                        int hit = 0;
                        for (int w = 0; w < 2 && !hit; w++) {
                            const int wi = 1 + 2 * w, d = before.rear[w].dispersion;
                            const int32_t bx = carried ? i32(pr, 0x574 + 0x70 * wi + 0x38) : i32(pr, 0x5a0 + 0x70 * wi);
                            const int32_t bz = carried ? i32(pr, 0x574 + 0x70 * wi + 0x40) : i32(pr, 0x5a8 + 0x70 * wi);
                            for (int t = (int)T - 4; t <= (int)T && !hit; t++) {
                                const int s = ((2 * d) & t) - d;
                                if (gx == bx + s * X7 && gz == bz + s * Z7) hit = 1;
                            }
                        }
                        spawns_ok += hit;
                        if (!hit) FAIL("frame %d car %d kind %d record %d: spawned at (%d, %d), no corner and counter value of the frame explain it (car %d %d, dispersion %d %d)", fi, i, k, j,
                                       gx, gz, i32(pr, 0x4d8), i32(pr, 0x4e0), before.rear[0].dispersion, before.rear[1].dispersion);
                        const float put[3] = { (float)gx, (float)i32(q, 0x1C), (float)gz };
                        s2fx_puff_load(i, k, j, 1, age, put, carried);       /* (from here on the place is the game's: the spread's own chance is out of the way) */
                    }
                    if (alive_prev[i][k][j] && !on) {
                        ended++;
                        ended_30 += fi - born[i][k][j] == 30;
                    }
                    alive_prev[i][k][j] = on;
                }
        }
        frames_ok += alive_game == s2fx_puffs();
        if (render) {
            if (fi >= first) render_frame(fi, fr, cp, argv[9]);
            continue;
        }
        /* the places: the objects the firmware hands to the engine, in its order (per car the shadow, then the alive puffs by kind and record) */
        static struct track_object objs[S2FX_MAX];
        const struct track_cam cam = camera_of(fr);
        const int no = s2fx_objects(&cam, cp, CARS, objs, S2FX_MAX);
        if (getenv("COST")) {                                    /* what the effects cost the unit: the pixels their pieces draw (a blended one takes two clocks), their faces */
            const struct rs_clip clip = { 0, 0, FB_W, FB_H };
            struct su_view v;
            track_view(&clip, &v);
            uint32_t px_s = 0, px_p = 0;
            struct gu_stats gst = { 0, 0, 0, 0 };
            for (int m = 0; m < no; m++)
                for (int b = 0; b < objs[m].n; b++) {
                    struct gu_piece g;
                    track_block_piece(&cam, &objs[m].b[b], &g);
                    const uint32_t px = gu_draw(FB(1), &clip, &v, track_mlb(), &g, &gst);
                    if (objs[m].key) px_s += px; else px_p += px;
                }
            fx_pieces += (uint32_t)no;
            fx_px_sum += px_s + px_p;
            fx_faces_sum += gst.faces;
            if (px_s + px_p > fx_px_max) { fx_px_max = px_s + px_p; fx_px_max_at = fi; }
            if (gst.faces > fx_faces_max) fx_faces_max = gst.faces;
        }
        int at = 0;
        for (int i = 0; i < CARS; i++) {
            const uint8_t *r = fr + 12 + REC * i;
            if (fx.shadow) {                                      /* the shadow: where the game's object record has it (0x1f4a7) */
                const struct track_object *o = at < no ? &objs[at] : 0;
                at++;
                if (has_objs && rec.nobj >= 170) {
                    const size_t so = 12 + 4 * REC + 0x1C * (size_t)rec.nobj + 0xAEC + (size_t)i * 17 * 0x40;
                    const int32_t gx = i32(fr, (uint32_t)so + 8), gy = i32(fr, (uint32_t)so + 0xC), gz = i32(fr, (uint32_t)so + 0x10);
                    if (!gx && !gy && !gz) goto no_shadow;                /* (the game has not set it up yet) */
                    shadows++;
                    /* (the game works the place in the x87's extended precision and cuts it; the firmware's float arithmetic is a unit off in about one frame in a hundred) */
                    const int ok = o && abs((int32_t)o->x - gx) <= 1 && (int32_t)o->h == gy && abs((int32_t)o->y - gz) <= 1;
                    shadows_ok += ok;
                    shadows_exact += o && (int32_t)o->x == gx && (int32_t)o->y == gz;
                    if (!ok) FAIL("frame %d car %d: the shadow stands at (%d %d %d), the game's at (%d %d %d)", fi, i, o ? (int)o->x : 0, o ? (int)o->h : 0, o ? (int)o->y : 0, gx, gy, gz);
                no_shadow:;
                }
            }
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < S2FX_PUFFS; j++) {
                    const uint8_t *q = r + POOL(k) + 0x28 * j;
                    if (!i32(q, 8)) continue;
                    float p[3], age;
                    int carried;
                    s2fx_puff_state(i, k, j, p, &age, &carried);
                    const struct track_object *o = at < no ? &objs[at] : 0;
                    at++;
                    int32_t wx = i32(q, 0x18), wy = i32(q, 0x1C), wz = i32(q, 0x20);
                    if (carried) {
                        wx = i32(r, 0x4d8) + i32(q, 0xC);
                        wy = i32(r, 0x4dc) + i32(q, 0x10);
                        wz = i32(r, 0x4e0) + i32(q, 0x14);
                        carried_n++;
                    }
                    places++;
                    /* (a carried puff's place is worked in floats here and in the x87's extended precision there, cut to whole numbers: a unit apart in about one place in 4,000) */
                    const int exact = o && (int32_t)o->x == wx && (int32_t)o->h == wy && (int32_t)o->y == wz;
                    const int ok = o && abs((int32_t)o->x - wx) <= 1 && abs((int32_t)o->h - wy) <= 1 && abs((int32_t)o->y - wz) <= 1 && (carried || exact);
                    places_ok += exact;
                    places_unit += ok;
                    carried_ok += ok && carried;
                    if (!ok) FAIL("frame %d car %d kind %d record %d: the firmware's place (%d %d %d) is not the game's (%d %d %d)%s", fi, i, k, j,
                                  o ? (int)o->x : 0, o ? (int)o->h : 0, o ? (int)o->y : 0, wx, wy, wz, carried ? " (carried)" : "");
                    const size_t oo = 12 + 4 * REC + 0x1C * (size_t)rec.nobj + 0xAEC + (size_t)(i * 17 + 1 + k * 8 + j) * 0x40;      /* the puff's object record: +0x20 its age, 65536 a life */
                    if (has_objs) {
                        ages++;
                        const int diff = (int)(age * 65536.0f) - i32(fr, (uint32_t)oo + 0x20);
                        ages_ok += diff >= -3 && diff <= 3;
                        if (diff < -3 || diff > 3) FAIL("frame %d car %d kind %d record %d: age %d, the game's %d", fi, i, k, j, (int)(age * 65536.0f), i32(fr, (uint32_t)oo + 0x20));
                    }
                }
        }
    }
    if (!render) {
        printf("%d frames: the puffs alive equal the game's in %d of them (%d records wrong); %d spawns, %d at a place the game's rule gives; %d places compared (%d carried: %d equal), %d equal, %d within a unit; "
               "%d ages compared with the objects' records, %d equal; %d shadows compared, %d within a unit, %d exactly; %d puffs ended, %d of them after 30 frames; %d frames in which the game worked a step twice (timer shows it), %d more in which its puffs aged a step more; %d wrong in all\n",
               rec.n, frames_ok, wrong_on, spawns, spawns_ok, places, carried_n, carried_ok, places_ok, places_unit, ages, ages_ok, shadows, shadows_ok, shadows_exact, ended, ended_30, doubles, silent, bad);
        if (getenv("COST")) printf("the effects' pieces (shadows and puffs): %.1f a frame on average, the faces %.1f on average and %u at most; the pixels their faces draw (overdraw included, each blended: two clocks of the rasterizer) %.0f on average, %u at most (frame %d), %.2f ms of the rasterizer's clocks at the most\n", (double)fx_pieces / rec.n, (double)fx_faces_sum / rec.n, fx_faces_max, (double)fx_px_sum / rec.n, fx_px_max, fx_px_max_at, 2.0 * fx_px_max / 74250.0);
        const int pass = !bad && frames_ok == rec.n && ended >= ended_30 && (doubles + silent) * 20 <= rec.n && (!has_objs || (ages == ages_ok && shadows == shadows_ok && shadows_exact * 10 >= shadows * 9));
        printf(pass ? "PASS\n" : "FAIL\n");
        return !pass;
    }
    return 0;
}
