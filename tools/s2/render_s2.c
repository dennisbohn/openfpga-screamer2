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
 * Writes <prefix><k>.rgb (320 x 200 x 3) per camera and prints what the renderer reports. Every run also checks the car models' wheels
 * (check_models) and the cars' look: brake lights, wheels' blur, damage marks (check_looks); the exit code is 3 if one fails.
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

/* The scene as the viewer draws it: the backdrop and the track's pieces and objects seen from cam, into the frame buffer; the
   picture also copied into out (FB_W x FB_H palette indices) if it is not NULL */
static void draw_scene(const struct track_cam *cam, struct track_object *objs, int nobj, struct track_stats *ts, uint8_t *out)
{
    const struct rs_clip clip = { 0, 0, FB_W, FB_H };
    volatile uint8_t *fb = FB(0);
    static struct blit_list l;
    blit_start(&l, 0);
    track_backdrop_blit(&l, &clip, cam);
    blit_model(fb, &l);
    memset(ts, 0, sizeof *ts);
    track_mode = TRACK_MODEL;
    track_draw_cam(fb, &clip, cam, objs, nobj, ts);
    if (out)
        for (int i = 0; i < FB_W * FB_H; i++) out[i] = fb[i];
}

/* LOOK_DUMP=<path prefix> writes the pictures of check_looks as <prefix><name>.rgb (320 x 200 x 3) */
static void dump_pic(const char *name, const uint8_t *pic)
{
    const char *prefix = getenv("LOOK_DUMP");
    if (!prefix) return;
    char path[512];
    snprintf(path, sizeof path, "%s%s.rgb", prefix, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    const uint32_t *pal = track_palette();
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint8_t rgb[3] = { (uint8_t)(pal[pic[i]] >> 16), (uint8_t)(pal[pic[i]] >> 8), (uint8_t)pal[pic[i]] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

#define LOOK_DIST   900.0f
#define LOOK_UP     250.0f

/* A snapshot of the player's car (model 0) from around it: the camera az radians round from behind it, dist behind and up above */
static void snap(const struct s2car_state *car, float az, float dist, float up, uint8_t *out)
{
    static struct track_object obj;
    struct track_cam own, cam;
    struct track_stats ts;
    float d = car->yaw + az;
    road_drive_scene(car, &own, &obj);
    cam = (struct track_cam){ car->pos[0] - sinf(d) * dist, car->pos[1] + cosf(d) * dist, car->pos[2] + up, d, -0.2f, 0.0f };
    draw_scene(&cam, &obj, 1, &ts, out);
}

/* the pixels that differ, and how much brighter (the sum of the palette's red, green and blue) b is there than a */
static int pic_diff(const uint8_t *a, const uint8_t *b, int *brighter)
{
    const uint32_t *pal = track_palette();
    int n = 0;
    *brighter = 0;
    for (int i = 0; i < FB_W * FB_H; i++) {
        if (a[i] == b[i]) continue;
        uint32_t x = pal[a[i]], y = pal[b[i]];
        *brighter += (int)((y >> 16 & 255) + (y >> 8 & 255) + (y & 255)) - (int)((x >> 16 & 255) + (x >> 8 & 255) + (x & 255));
        n++;
    }
    return n;
}

/* The look of a car as its physics hands it to the drawing (s2car.h look; STARTL.EXE 0x16878 from 0x1737b: 0x200 while the brake is
   down, 0x400 / 0x8000 a pair of wheels turning fast, the marks of the four sides' damage 0x2000 0x800 0x4000 0x1000 from +0xb8; a
   texture's picture is flags >> 9) and what it does to the car as drawn. The player's car on the start line, drawn from four sides:
   standing, it is the whole car (look 0, picture 0; Screamer 1's rule made it show a scraped flank and no brake lights); braking, the
   rear lights are brighter and only they change; every mark and every pair of wheels changes the car from some side. Then the car at full
   throttle (the wheels blur, the brake lights stay dark) and a race (the grid's cars whole, the computers' cars brake). Returns the
   number of failures. LOOK_DUMP=<path prefix> writes the standing and braking pictures. */
static int check_looks(int tn)
{
    static struct s2car_state car;
    static uint8_t base[4][FB_W * FB_H], alt[FB_W * FB_H];
    static const uint32_t marks[4] = { 0x2000, 0x800, 0x4000, 0x1000 };
    const struct s2car_input none = { 0 }, brake = { .brake = 1.0f };
    const float az[4] = { 0.0f, 1.5707963f, 3.1415927f, -1.5707963f };
    struct car_look lk;
    int bad = 0, lit, n;
#define EXPECT(cond, ...) do { if (!(cond)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); bad++; } } while (0)
    if (!s2car_init(&car, 0, 0, tn, 0)) { printf("  FAIL: s2car_init (the game's files)\n"); return 1; }
    for (int k = 0; k < 3; k++) s2car_step(&car, &none);
    EXPECT(car.look == 0, "standing: look %x, not 0", (unsigned)car.look);
    s2car_look(&car, &lk);
    EXPECT(lk.phase == 0, "standing: the drawing's texture phase is %x, not 0", (unsigned)lk.phase);
    for (int k = 0; k < 4; k++) snap(&car, az[k], LOOK_DIST, LOOK_UP, base[k]);

    for (int k = 0; k < 3; k++) s2car_step(&car, &brake);
    EXPECT(car.look == 0x200, "braking: look %x, not 200", (unsigned)car.look);
    s2car_look(&car, &lk);
    EXPECT(lk.phase == 0x80, "braking: the drawing's texture phase is %x, not 80 (picture 1)", (unsigned)lk.phase);
    snap(&car, 0.0f, LOOK_DIST, LOOK_UP, alt);
    dump_pic("standing", base[0]);
    dump_pic("braking", alt);
    n = pic_diff(base[0], alt, &lit);
    printf("car look: braking changes %d pixels (%+d in light) seen from behind\n", n, lit);
    EXPECT(n > 0 && lit > 0, "braking does not light the rear lights");
    EXPECT(n < FB_W * FB_H / 20, "braking changes %d pixels: more than the rear lights", n);
    for (int k = 1; k < 4; k++) {                               /* from the front and the sides the lights are hidden: only the rear differs (some pixels) */
        snap(&car, az[k], LOOK_DIST, LOOK_UP, alt);
        n = pic_diff(base[k], alt, &lit);
        EXPECT(k == 2 ? n == 0 : n < FB_W * FB_H / 20, "braking from side %d changes %d pixels", k, n);
    }
    for (int k = 0; k < 3; k++) s2car_step(&car, &none);
    EXPECT(car.look == 0, "released: look %x, not 0", (unsigned)car.look);

    for (int m = 0; m < 4; m++) {                               /* a side's mark: the flags carry it and some side of the car shows it */
        car.rec[0xb8 / 4] = marks[m];
        s2car_step(&car, &none);
        EXPECT(car.look == marks[m], "mark %x: look %x", (unsigned)marks[m], (unsigned)car.look);
        s2car_look(&car, &lk);
        EXPECT(lk.phase == marks[m] >> 2, "mark %x: the drawing's texture phase is %x", (unsigned)marks[m], (unsigned)lk.phase);
        int shown = 0;
        for (int k = 0; k < 4; k++) {
            snap(&car, az[k], LOOK_DIST, LOOK_UP, alt);
            shown += pic_diff(base[k], alt, &lit) > 0;
        }
        printf("car look: mark %x is seen from %d of 4 sides\n", (unsigned)marks[m], shown);
        EXPECT(shown > 0, "mark %x changes nothing of the car", (unsigned)marks[m]);
    }
    car.rec[0xb8 / 4] = 0;
    s2car_step(&car, &none);
    for (int w = 0; w < 2; w++) {                               /* a pair of wheels' blur (poked: the physics' side is below) */
        const uint32_t flag = w ? 0x8000 : 0x400;
        int shown = 0;
        car.look = flag;
        for (int k = 1; k < 4; k += 2) {
            snap(&car, az[k], 500.0f, 60.0f, alt);
            shown += pic_diff(base[k], alt, &lit) > 0;
        }
        printf("car look: wheels' flag %x is seen from %d of 2 sides\n", (unsigned)flag, shown);
        EXPECT(shown > 0, "the wheels' flag %x changes nothing of the car", (unsigned)flag);
    }

    int seen[3] = { 0, 0, 0 };                                  /* the car at full throttle: the driven pair blurs first, then the other */
    uint32_t first[2] = { 0, 0 };
    car.hold = 0;
    for (int k = 0; k < 1500; k++) {
        const struct s2car_input gas = { .throttle = 1.0f };
        s2car_step(&car, &gas);
        for (int b = 0; b < 2; b++) if ((car.look & (b ? 0x8000u : 0x400u)) && !first[b]) first[b] = (uint32_t)k + 1;
        seen[2] |= (int)(car.look & 0x200);
    }
    printf("car look: full throttle: the rear (driven) wheels blur from step %u, the front ones from step %u\n", (unsigned)first[0], (unsigned)first[1]);
    EXPECT(first[0] && first[1] && first[0] <= first[1], "the wheels' blur at full throttle: rear %u front %u", (unsigned)first[0], (unsigned)first[1]);
    EXPECT(!seen[2], "the brake lights at full throttle");

    if (race_init(tn, road_floor() - 3000.0f)) {                /* the race: the grid's cars whole, the computers' cars' brake lights */
        int braked = 0, grid = 0, flags = 0;
        for (int k = 0; k < 1100; k++) {
            const struct s2car_input gas = { .throttle = 1.0f };
            race_step(&gas);
            for (int i = 0; i < race_cars(); i++) {
                const uint32_t lk = race_car(i)->look;
                grid += k < 60 && lk != 0;
                flags += (lk & ~0xFE00u) != 0;
                braked += i > 0 && (lk & 0x200) != 0;
            }
        }
        printf("car look: race: %d car steps on the grid with a look, %d computers' steps braking\n", grid, braked);
        EXPECT(grid == 0, "cars standing on the grid with a look (damaged or braking)");
        EXPECT(flags == 0, "a look with bits the game does not set");
        EXPECT(braked > 0, "the computers' cars never show their brake lights");
    } else {
        EXPECT(0, "race_init");
    }
#undef EXPECT
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
    int tn = 1;                                         /* the track of the image: the last digit 1 .. 7 of its file name (ref3.bin), else 1 */
    for (const char *q = argv[1]; *q; q++) if (*q >= '1' && *q <= '7') tn = *q - '0';
    failures += check_looks(tn);

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
            s2car_init(&car, 0, 0, tn, 0);
            for (int k = 0; k < steps; k++) {
                struct s2car_input in = { .throttle = k < 130, .brake = k >= 130, .steer = k >= 100 && k < 120 ? 1.0f : k >= 25 && k < 100 ? -1.0f : 0.0f };
                s2car_step(&car, &in);
                if (k % 25 == 0 || k == steps - 1)
                    printf("step %d: look %x %.1f mph gear %d yaw %.2f at %.0f %.0f %.0f%s\n", k + 1, (unsigned)car.look, car.speed_mph, car.gear, car.yaw,
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
                        printf("  [%d] L%d P%d %3.0fmph n%.1f%s look %x", i, (int)c->rec[0x4ac / 4], (int)c->rec[0xc30 / 4], c->speed_mph, dist,
                               c->off_track ? "!" : "", (unsigned)c->look);
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
        struct track_stats ts;
        draw_scene(&cam, objs, nobj, &ts, 0);
        char name[512];
        snprintf(name, sizeof name, "%s%d.rgb", argv[2], a - 3);
        write_rgb(name, track_palette());
        printf("camera %.0f %.0f %.0f yaw %.3f: %u pieces handed on, %u faces, %u triangles, %u vertices, %u see-through, %u objects; %s\n",
               cam.x, cam.y, cam.h, cam.yaw, ts.pieces, ts.faces, ts.triangles, ts.vertices, ts.see_through, ts.objects, name);
    }
    if (failures) { printf("%d pose check failures\n", failures); return 3; }
    return 0;
}
