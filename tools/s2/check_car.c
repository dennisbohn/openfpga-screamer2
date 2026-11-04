/*
 * Host test of the Screamer 2 car physics (firmware/s2car.c) against a reference trace: the same scenario (keys by
 * step) is run through the firmware's float32 car, one 20 ms step at a time, and compared row by row with the CSV
 * that OpenScreamer's converted game routines wrote for it (the ground truth; tools/s2/check_car.sh says where
 * it is and how to make it). Built and run by tools/s2/check_car.sh together with the firmware's track.c and the
 * C models, natively in Docker; not part of the firmware.
 *
 * Usage: check_car <game folder> <track image> <scenario> <trace.csv> <track> <team> <model> <flat kind or -1> [ours.csv]
 *   scenario  the reference's file: "@steps N", "@flat K", "@put x,y,heading", "@auto 0|1", then "<step> <keys>"
 *             lines (keys A accelerate, B brake, L left, R right, U gear up, D gear down; from that step on, counting the first
 *             step as 0, whose result is row 1 of the trace)
 *   flat kind a level world of that road kind with no walls and no road points (the ground and the road points
 *             of the track are replaced), as the reference's OS_S2FLAT
 * Prints one line per quantity: the largest difference from the reference over the steps, where, and the mean.
 */
#include "../../firmware/s2car.h"
#include "../../firmware/track.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the ground and the road replaced by a level world (ld --wrap) */
static int g_flat = -1;
int __real_track_ground_s2(int x, int y, int only_whether, int *height, int normal[3], int *kind);
const struct track_dir *__real_track_dirs(int *n);
const struct track_wall *__real_track_walls(int *n);
int __wrap_track_ground_s2(int x, int y, int only_whether, int *height, int normal[3], int *kind)
{
    if(g_flat < 0) return __real_track_ground_s2(x, y, only_whether, height, normal, kind);
    *height = 0; normal[0] = 0; normal[1] = 16384; normal[2] = 0; *kind = g_flat;
    return 1;
}
const struct track_dir *__wrap_track_dirs(int *n)
{
    if(g_flat >= 0) { *n = 0; return 0; }
    return __real_track_dirs(n);
}
const struct track_wall *__wrap_track_walls(int *n)
{
    if(g_flat >= 0) { *n = 0; return 0; }
    return __real_track_walls(n);
}

static uint8_t *slurp(const char *dir, const char *name, uint32_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s%s%s", dir, *dir ? "/" : "", name);
    FILE *f = fopen(path, "rb");
    if(!f) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = aligned_alloc(64, ((size_t)n + 63) & ~(size_t)63);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

/* the reference's columns we compare */
enum { C_X, C_Y, C_H, C_HEADING, C_PITCH, C_ROLL, C_SPEED, C_RPM, C_GEAR, C_THROTTLE, C_STEER, C_WHEELS, C_BODYP, C_BODYR,
       C_SLIP, C_SPIN, C_STEERSHOWN, C_GROUND, C_GPITCH, C_GROLL, C_WGY, C_WKIND, C_WPX, C_WPY, C_WCX, C_WCY, C_WSPR, C_WALL, C_KNOCK, C_WSPIN, C_N };
static const char *const col_name[C_N] = { "x", "y", "height", "heading", "pitch", "roll", "speed", "rpm", "gear", "throttle",
                                           "steer", "wheels_down", "body_pitch", "body_roll", "slip_angle", "w0_spin", "steer_shown",
                                           "ground", "ground_pitch", "ground_roll", "w0_ground_y", "w0_kind", "w0_push_x", "w0_push_y", "w0_cx", "w0_cy", "w0_spring", "wall", "knock", "spin" };
#define col_csv col_name                  /* the reference's header names are ours */
/* a column is an angle when its difference is taken the short way round */
static int is_angle(int c) { return c == C_HEADING || c == C_PITCH || c == C_ROLL || c == C_SLIP; }

#define MAX_ROWS 4096
static double ref[MAX_ROWS][C_N], ours[MAX_ROWS][C_N];

static int read_trace(const char *path)
{
    FILE *f = fopen(path, "r");
    if(!f) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    static char line[1 << 16];
    if(!fgets(line, sizeof line, f)) exit(2);
    int idx[C_N];
    for(int c = 0; c < C_N; c++) idx[c] = -1;
    int n = 0;
    for(char *p = line; p; n++) {
        char *e = strpbrk(p, ",\r\n");
        size_t len = e ? (size_t)(e - p) : strlen(p);
        for(int c = 0; c < C_N; c++) if(strlen(col_csv[c]) == len && !strncmp(p, col_csv[c], len)) idx[c] = n;
        p = e && *e == ',' ? e + 1 : 0;
    }
    int rows = 0;
    while(fgets(line, sizeof line, f) && rows < MAX_ROWS) {
        int k = 0;
        for(char *p = line; p; k++) {
            char *e = strchr(p, ',');
            for(int c = 0; c < C_N; c++) if(idx[c] == k) ref[rows][c] = atof(p);
            p = e ? e + 1 : 0;
        }
        rows++;
    }
    fclose(f);
    return rows;
}

/* the game's keys for step k (1 ..): the last line of the scenario at or before it */
struct line { int step; char keys[8]; };

static void record(struct s2car_state *s, int row, const struct s2car_input *in)
{
    (void)in;
    union { uint32_t u; float f; int32_t i; } *w = (void *)s->rec;
#define RF(o) w[(o) >> 2].f
#define RI(o) w[(o) >> 2].i
    ours[row][C_X] = RI(0x4d8);
    ours[row][C_Y] = RI(0x4e0);
    ours[row][C_H] = RI(0x4dc);
    ours[row][C_HEADING] = RF(0x4e8);
    ours[row][C_PITCH] = RF(0x4e4);
    ours[row][C_ROLL] = RF(0x4ec);
    ours[row][C_SPEED] = RF(0x500);
    ours[row][C_RPM] = RF(0xc78);
    ours[row][C_GEAR] = RI(0xc7c);
    ours[row][C_THROTTLE] = RF(0x1ec);
    ours[row][C_STEER] = RF(0xbc);
    ours[row][C_WHEELS] = RF(0x510);
    ours[row][C_BODYP] = RF(0xc18);
    ours[row][C_BODYR] = RF(0xc20);
    ours[row][C_SLIP] = RF(0x4d4);
    ours[row][C_SPIN] = RF(0x584);
    ours[row][C_STEERSHOWN] = RF(0x13c);
    ours[row][C_GROUND] = RI(0x4f8);
    ours[row][C_GPITCH] = RF(0xc0c);
    ours[row][C_GROLL] = RF(0xc14);
    ours[row][C_WGY] = RI(0x574 + 0x44);
    ours[row][C_WKIND] = RI(0x574 + 0x5c);
    ours[row][C_WPX] = RF(0x544);
    ours[row][C_WPY] = RF(0x544 + 8);
    ours[row][C_WCX] = RF(0x514);
    ours[row][C_WCY] = RF(0x514 + 8);
    ours[row][C_WSPR] = RF(0x574 + 0x14);
    ours[row][C_WALL] = RI(0x150);
    ours[row][C_KNOCK] = RF(0x30);
    ours[row][C_WSPIN] = RI(0x58);
}

int main(int argc, char **argv)
{
    if(argc < 9) {
        fprintf(stderr, "usage: %s <game folder> <track image> <scenario> <trace.csv> <track> <team> <model> <flat kind or -1> [ours.csv]\n", argv[0]);
        return 2;
    }
    const char *game = argv[1];
    int track = atoi(argv[5]), team = atoi(argv[6]), model = atoi(argv[7]), flat = atoi(argv[8]);
    uint32_t size, gsize, msize;
    uint8_t *img = slurp("", argv[2], &size);
    if(!track_open(img) || !track_init()) { fprintf(stderr, "image not usable\n"); return 2; }
    char name[64];
    uint8_t *gdat = slurp(game, "DATA/GAME.DAT", &gsize);
    snprintf(name, sizeof name, "DATA/MAP%d.DAT", track);
    uint8_t *mdat = slurp(game, name, &msize);
    s2car_set_data(gdat, gsize, mdat, msize);

    /* the scenario */
    FILE *sf = fopen(argv[3], "r");
    if(!sf) { fprintf(stderr, "cannot read %s\n", argv[3]); return 2; }
    static struct line lines[256];
    int nlines = 0, steps = 0, put = 0, manual = 0;
    double px = 0, py = 0, ph = 0;
    char buf[256];
    while(fgets(buf, sizeof buf, sf)) {
        if(buf[0] == '#' || buf[0] == '\n') continue;
        if(!strncmp(buf, "@steps", 6)) steps = atoi(buf + 6);
        else if(!strncmp(buf, "@put", 4)) { put = sscanf(buf + 4, "%lf,%lf,%lf", &px, &py, &ph) == 3; }
        else if(!strncmp(buf, "@auto", 5)) manual = atoi(buf + 5) == 0;
        else if(buf[0] == '@') continue;
        else if(nlines < 256) {
            lines[nlines].keys[0] = 0;
            if(sscanf(buf, "%d %7s", &lines[nlines].step, lines[nlines].keys) >= 1) nlines++;
        }
    }
    fclose(sf);
    int rows = read_trace(argv[4]);
    if(steps + 1 < rows) rows = steps + 1;

    g_flat = flat;
    static struct s2car_state s;
    s.manual = manual;
    if(!s2car_init(&s, team, model, track, 0)) { fprintf(stderr, "s2car_init failed\n"); return 2; }
    if(put) s2car_put(&s, (int)px, (int)py, (float)ph);       /* the reference's s2_put() */
    struct s2car_input none = { 0 };
    record(&s, 0, &none);
    for(int k = 1; k < rows; k++) {
        const char *keys = "";
        for(int i = 0; i < nlines; i++) if(lines[i].step <= k - 1) keys = lines[i].keys;   /* step 0 is the first step: row 1 */
        struct s2car_input in = { 0 };
        for(const char *p = keys; *p; p++) {
            if(*p == 'A') in.throttle = 1.0f;
            else if(*p == 'B') in.brake = 1.0f;
            else if(*p == 'L') in.steer = -1.0f;
            else if(*p == 'R') in.steer = 1.0f;
            else if(*p == 'U') in.gear_up = 1;
            else if(*p == 'D') in.gear_down = 1;
        }
        s2car_step(&s, &in);
        record(&s, k, &in);
    }
    printf("%-12s %12s %8s %12s   (%d steps: scenario %s)\n", "quantity", "max |diff|", "at step", "mean |diff|", rows - 1, argv[3]);
    for(int c = 0; c < C_N; c++) {
        double worst = 0, sum = 0;
        int at = 0;
        for(int k = 0; k < rows; k++) {
            double d = ours[k][c] - ref[k][c];
            if(is_angle(c)) { while(d > M_PI) d -= 2 * M_PI; while(d < -M_PI) d += 2 * M_PI; }
            d = fabs(d);
            sum += d;
            if(d > worst) { worst = d; at = k; }
        }
        printf("%-12s %12.5g %8d %12.5g\n", col_name[c], worst, at, sum / rows);
    }
    if(argc > 9) {
        FILE *o = fopen(argv[9], "w");
        if(o) {
            fprintf(o, "step");
            for(int c = 0; c < C_N; c++) fprintf(o, ",ours_%s,ref_%s", col_name[c], col_name[c]);
            fprintf(o, "\n");
            for(int k = 0; k < rows; k++) {
                fprintf(o, "%d", k);
                for(int c = 0; c < C_N; c++) fprintf(o, ",%.9g,%.9g", ours[k][c], ref[k][c]);
                fprintf(o, "\n");
            }
            fclose(o);
        }
    }
    return 0;
}
