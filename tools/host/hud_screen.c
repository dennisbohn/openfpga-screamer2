/*
 * The race screen (firmware/s2hud.c) on the PC: the same code that runs on the Pocket, over a made-up race (the car records race.c keeps,
 * set by the scenario), with the blitter's C model for the hardware. Called by tools/hud_screens.sh with the scenario and the expected
 * frames of tools/hud_reference.py, which are made from the game's files and the original's code, not from the firmware.
 *
 * The scenario (<ref dir>/scenario.txt) is one command a line:
 *   palette <file>                      the 256 colours (little-endian words 0x00RRGGBB) the frames are shown in
 *   background <file>                   a picture of 64,000 palette indices under the screen (a race picture)
 *   begin <track> <laps> <rec_lap> <rec_total>   a race begins (s2hud_begin), the track's best lap and total time as the save has them
 *   car <i> <name>=<number> ...         fields of car i's record: team (1 .. 4), n4b8, n4bc, n4d8, n4e0 (floats), place, gear, flags, lapcnt
 *                                       (ints), total, lapt, best, speed, rpm, maxrpm, wrong, px, py, ph (floats)
 *   clock <seconds>                     the race's clock (the start lights)
 *   camera <x> <y> <h> <yaw> <pitch> <roll>
 *   frame <name> <steps> <top> <expected>  one s2hud_draw() of `steps` 20 ms steps; the picture is saved as <prefix>_<name>.rgb and, unless
 *                                       the expected file is -, must equal it (320 x 200 palette indices) in every pixel
 *   expect_cmd <name> <n> <x> <y> <tol> the frame's blitter list must hold a sprite of the leader's mark (command with the 10-bit x, y of
 *                                       the last frame) within tol pixels of x, y; n: the number of commands of the list must be < 128
 * Exit code 0 if all passed.
 */
#define _GNU_SOURCE
#include "../s2hud.h"
#include "../s2gfx.h"
#include "../s2car.h"
#include "../race.h"
#include "../track.h"
#include "../setup.h"
#include "../frame.h"
#include "../s2load.h"
#include "../dlog.h"
#include "../hw.h"
#include "../slot.h"
#include "../zip.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define SCRATCH     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000))

static FILE *zf, *zf2;                  /* slot 3: the ZIP; slot 4: one with GAME1.DAT deflated (the same scenario must give the same pictures) */
static uint32_t zsize, zsize2;
static struct zip zip, zip2;
static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------------------------------------ what the firmware is linked against */

uint32_t slot_size(uint32_t id) { return id == 3 ? zsize : id == 4 ? zsize2 : 0; }
void slot_set_size(uint32_t id, uint32_t size) { (void)id; (void)size; }

#define OUTSIDE 0xE0000000u
static uint8_t *outside;

uint32_t zip_addr(const void *p)
{
    if ((uintptr_t)p >= SDRAM_BASE && (uintptr_t)p < SDRAM_BASE + SDRAM_SIZE) return (uint32_t)(uintptr_t)p;
    outside = (uint8_t *)p;
    return OUTSIDE;
}

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    void *to = addr == OUTSIDE ? outside : (void *)(uintptr_t)addr;
    FILE *f = id == 4 ? zf2 : zf;
    uint32_t size = id == 4 ? zsize2 : zsize;
    if ((id != 3 && id != 4) || (off & 3) || off >= size || (len != SLOT_REST && ((len & 3) || off + len > size))) return 2;
    uint32_t n = len == SLOT_REST ? size - off : len;
    memset(to, 0, (n + 3) & ~3u);
    return fseek(f, off, SEEK_SET) || fread(to, 1, n, f) != n ? 2 : 0;
}

uint32_t draw_buf;
static struct blit_list captured;
void hw_blit(const struct blit_list *l) { captured = *l; }

/* the race: four cars' records, the clock; race_get_hud() as race.c has it */
struct s2_pick race_grid[RACE_CARS] = { { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } };
int race_laps = 3;
static struct s2car_state cars[RACE_CARS];
static float race_clock;
static int line_nodes = 36;
int race_kind = RACE_NORMAL;
static struct race_free free_state;
const struct race_free *race_free_state(void) { return &free_state; }
struct s2car_state *race_car(int i) { return &cars[i]; }
int race_cars(void) { return race_kind == RACE_NORMAL ? RACE_CARS : 1; }
int race_line_nodes(void) { return line_nodes; }

union word { int32_t i; uint32_t u; float f; };
#define F(c, o) (((union word *)(c)->rec)[(o) >> 2].f)
#define I(c, o) (((union word *)(c)->rec)[(o) >> 2].i)

void race_get_hud(struct race_hud *h)
{
    const struct s2car_state *c = &cars[0];
    int lap = I(c, 0x4ac) + 1;
    h->count = race_clock < 4.0f ? 3 - (int)race_clock : -1;
    h->laps = race_laps;
    h->lap = lap > race_laps ? race_laps : lap;
    h->place = I(c, 0xc30);
    h->cars = RACE_CARS;
    h->finished = (I(c, 0x4c) & 2) != 0;
    h->time = F(c, 0xc38);
    h->lap_time = F(c, 0xc3c);
    h->best_lap = F(c, 0xc40) < 3599.0f ? F(c, 0xc40) : -1.0f;
}

/* the track's DATA container, for the bends (MAPnDIR.SPL) */
static uint8_t *map_data;
static uint32_t map_size;
const uint8_t *s2car_map_dat(uint32_t *size)
{
    *size = map_size;
    return map_data;
}

static float rec_lap_now, rec_total_now;
void s2_best_get(int track, int laps, struct s2_record *total, struct s2_record *lap)
{
    (void)track;
    (void)laps;
    *total = (struct s2_record){ rec_total_now, "MIL" };
    *lap = (struct s2_record){ rec_lap_now, "MIL" };
}
void s2_best_get_kind(int kind, int track, int laps, struct s2_record *total, struct s2_record *lap)
{
    (void)kind;
    s2_best_get(track, laps, total, lap);
}

/* track.c's camera frame (cam_frame), without its memory of the last angles */
void track_cam_axes(const struct track_cam *cam, float f[3], float r[3], float u[3])
{
    float sy = sinf(cam->yaw), cy = cosf(cam->yaw), sp = sinf(cam->pitch), cp = cosf(cam->pitch);
    float sr = sinf(cam->roll), cr = cosf(cam->roll);
    f[0] = sy * cp; f[1] = -cy * cp; f[2] = sp;
    const float r0[3] = { -cy, -sy, 0.0f };
    const float u0[3] = { r0[1] * f[2], -r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0] };
    r[0] = r0[0] * cr + u0[0] * sr; r[1] = r0[1] * cr + u0[1] * sr; r[2] = u0[2] * sr;
    u[0] = u0[0] * cr - r0[0] * sr; u[1] = u0[1] * cr - r0[1] * sr; u[2] = u0[2] * cr;
}

uint32_t wait_swap_done(void) { return 0; }
void pal_faded(const uint32_t *pal, int32_t f) { (void)pal; (void)f; }

/* ------------------------------------------------------------------------------------------------ the scenario */

static uint8_t *slurp(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    *n = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 8);
    if (fread(b, 1, *n, f) != *n) { free(b); b = 0; }
    fclose(f);
    return b;
}

static void save(const char *prefix, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s.rgb", prefix, name);
    FILE *o = fopen(path, "wb");
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t col = VID_PAL[fb[i]];
        uint8_t rgb[3] = { (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

static void set_car(int i, char *args)
{
    struct s2car_state *c = &cars[i];
    for (char *tok = strtok(args, " "); tok; tok = strtok(0, " ")) {
        char *eq = strchr(tok, '=');
        if (!eq) { CHECK(0, "bad field %s", tok); continue; }
        *eq = 0;
        double v = atof(eq + 1);
        const char *k = tok;
        if (!strcmp(k, "team")) race_grid[i].team = (uint8_t)v;
        else if (!strcmp(k, "n4b8")) F(c, 0x4b8) = (float)v;
        else if (!strcmp(k, "n4bc")) F(c, 0x4bc) = (float)v;
        else if (!strcmp(k, "n4d8")) F(c, 0x4d8) = (float)v;
        else if (!strcmp(k, "n4e0")) F(c, 0x4e0) = (float)v;
        else if (!strcmp(k, "place")) I(c, 0xc30) = (int)v;
        else if (!strcmp(k, "gear")) I(c, 0xc7c) = (int)v;
        else if (!strcmp(k, "flags")) I(c, 0x4c) = (int)v;
        else if (!strcmp(k, "lapcnt")) I(c, 0x4ac) = (int)v;
        else if (!strcmp(k, "total")) F(c, 0xc38) = (float)v;
        else if (!strcmp(k, "lapt")) F(c, 0xc3c) = (float)v;
        else if (!strcmp(k, "best")) F(c, 0xc40) = (float)v;
        else if (!strcmp(k, "speed")) F(c, 0x500) = (float)v;
        else if (!strcmp(k, "rpm")) F(c, 0xc78) = (float)v;
        else if (!strcmp(k, "maxrpm")) F(c, 0x60) = (float)v;
        else if (!strcmp(k, "wrong")) F(c, 0x1f0) = (float)v;
        else if (!strcmp(k, "px")) c->pos[0] = (float)v;
        else if (!strcmp(k, "py")) c->pos[1] = (float)v;
        else if (!strcmp(k, "ph")) c->pos[2] = (float)v;
        else CHECK(0, "unknown field %s", k);
    }
}

static int load_map(int track)
{
    char name[24];
    snprintf(name, sizeof name, "DATA/MAP%d.DAT", track);
    const struct zip_entry *e = zip_find(&zip, name);
    if (!e) return 0;
    free(map_data);
    map_data = malloc(e->size + 64);
    uint8_t *buf = (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D00000);
    if (zip_read(&zip, e, buf, 0x40000)) return 0;
    memcpy(map_data, buf, e->size);
    map_size = e->size;
    return 1;
}

static int run_scenario(const char *ref, const char *prefix)
{
    char path[512], line[1024];
    snprintf(path, sizeof path, "%s/scenario.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no scenario in %s", ref);
    if (!f) return 0;
    static uint32_t pal[256];
    static uint8_t background[64000];
    struct track_cam cam = { 0, 0, 0, 0, 0, 0 };
    const struct su_view view = { 159 * 16, 130 * 16, 128 * 16, 0 };
    int frames = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char a[256], b[256], c[256];
        int i, n, steps, top;
        float x, y, h, yaw, pitch, roll, rl, rt, secs;
        if (!*line || *line == '#') continue;
        if (sscanf(line, "palette %255s", a) == 1) {
            snprintf(path, sizeof path, "%s/%s", ref, a);
            uint32_t len;
            uint8_t *p = slurp(path, &len);
            CHECK(p && len == 1024, "palette %s", path);
            if (p) { memcpy(pal, p, 1024); free(p); }
        } else if (sscanf(line, "background %255s", a) == 1) {
            snprintf(path, sizeof path, "%s/%s", ref, a);
            uint32_t len;
            uint8_t *p = slurp(path, &len);
            CHECK(p && len == 64000, "background %s", path);
            if (p) { memcpy(background, p, 64000); free(p); }
        } else if (sscanf(line, "begin %d %d %f %f", &n, &steps, &rl, &rt) == 4) {
            memset(cars, 0, sizeof cars);
            race_laps = steps;
            rec_lap_now = rl;
            rec_total_now = rt;
            race_clock = 0.0f;
            for (int k = 0; k < RACE_CARS; k++) {
                race_grid[k].team = (uint8_t)(k + 1);
                F(&cars[k], 0xc40) = 3599.999f;
                F(&cars[k], 0x60) = 1.0f;
            }
            CHECK(load_map(n), "DATA/MAP%d.DAT", n);
            s2hud_begin(n);
        } else if (sscanf(line, "car %d %n", &i, &n) == 1 && i >= 0 && i < RACE_CARS) {
            strcpy(a, line + n);
            set_car(i, a);
        } else if (sscanf(line, "clock %f", &secs) == 1) {
            race_clock = secs;
        } else if (sscanf(line, "camera %f %f %f %f %f %f", &x, &y, &h, &yaw, &pitch, &roll) == 6) {
            cam = (struct track_cam){ x, y, h, yaw, pitch, roll };
        } else if (sscanf(line, "frame %255s %d %d %255s", a, &steps, &top, b) == 4) {
            volatile uint8_t *fb = FB(0);
            for (int k = 0; k < 64000; k++) fb[k] = background[k];
            for (int k = 0; k < 256; k++) VID_PAL[k] = pal[k];
            draw_buf = 0;
            s2hud_draw(&cam, &view, (uint32_t)steps, top);
            CHECK(captured.n < 128, "frame %s: %d blitter commands", a, captured.n);
            blit_model(fb, &captured);
            if (top) {                                              /* the timing overlay's two lines (22 characters of 8 x 8 at most) stay clear */
                int touched = 0;
                for (int py = 0; py < 16; py++)
                    for (int px = 0; px < 176; px++) touched += fb[py * 320 + px] != background[py * 320 + px];
                CHECK(!touched, "frame %s: %d pixels of the overlay's box are drawn over (top %d)", a, touched, top);
            }
            save(prefix, a);
            frames++;
            if (strcmp(b, "-")) {
                snprintf(path, sizeof path, "%s/%s", ref, b);
                uint32_t len;
                uint8_t *want = slurp(path, &len);
                CHECK(want && len == 64000, "expected frame %s", path);
                if (want) {
                    int differ = 0, first = -1;
                    for (int k = 0; k < 64000; k++)
                        if (fb[k] != want[k] && differ++ == 0) first = k;
                    CHECK(!differ, "frame %s: %d pixels differ from the reference, first at x %d y %d (%02x, expected %02x)", a, differ, first % 320,
                          first / 320, first >= 0 ? fb[first] : 0, first >= 0 ? want[first] : 0);
                    free(want);
                }
            }
        } else if (sscanf(line, "expect_cmd %255s %d %f %f %f", c, &n, &x, &y, &roll) == 5) {
            /* the last command of the list that is a copy at (x, y) within tol */
            int found = 0, seen_x = 0, seen_y = 0;
            for (int k = 0; k < captured.n; k++) {
                const uint32_t *w = captured.c[k];
                int kind = (int)(w[1] >> 28 & 3);
                if (kind != BLIT_COPY || (w[2] & 0x1FF) != 15 || (w[2] >> 16 & 0x1FF) != 11) continue;   /* sprite 0x26 is 15 x 11 */
                int px = (int)(w[1] & 0x3FF), py = (int)(w[1] >> 16 & 0x3FF);
                px = px & 0x200 ? px - 0x400 : px;
                py = py & 0x200 ? py - 0x400 : py;
                seen_x = px;
                seen_y = py;
                if (fabsf((float)px - x) <= roll && fabsf((float)py - y) <= roll) found = 1;
            }
            CHECK(found, "%s: the 1ST mark is at (%d, %d), expected (%.0f, %.0f) +-%.0f", c, seen_x, seen_y, x, y, roll);
        } else {
            CHECK(0, "scenario line not understood: %s", line);
        }
        (void)i;
    }
    fclose(f);
    printf("hud: %d frames drawn by the firmware's code, each equal to the independent reference\n", frames);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <out prefix> <reference dir> [<deflated ZIP>]\n", argv[0]); return 2; }
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE)
        || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    if (!(zf = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    fseek(zf, 0, SEEK_END);
    zsize = (uint32_t)ftell(zf);
    dlog_init();
    *(volatile uint32_t *)0xF000000Cu = 0x1Fu;                      /* MEM_STATUS: all writes done, the SDRAM ready */
    int r = zip_open(&zip, 3, SCRATCH, SCRATCH_MAX);
    CHECK(r == ZIP_OK, "zip_open %d", r);
    CHECK(s2gfx_init(&zip), "s2gfx_init (the texts of LOCAL.DAT)");
    CHECK(s2hud_init(&zip) && s2hud_ready(), "s2hud_init");
    run_scenario(argv[3], argv[2]);
    if (argc > 4 && (zf2 = fopen(argv[4], "rb"))) {                 /* GAME1.DAT deflated: inflated whole for the read, the screen the same */
        fseek(zf2, 0, SEEK_END);
        zsize2 = (uint32_t)ftell(zf2);
        CHECK(zip_open(&zip2, 4, SCRATCH, SCRATCH_MAX) == ZIP_OK, "zip_open of the deflated ZIP");
        CHECK(s2hud_init(&zip2) && s2hud_ready(), "s2hud_init on the deflated ZIP");
        run_scenario(argv[3], argv[2]);
    }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    printf(failures ? "hud: %d tests FAILED\n" : "hud: all tests passed\n", failures);
    return failures != 0;
}
