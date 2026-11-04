/*
 * The viewer (viewer.h)
 */
#include "viewer.h"
#include "frame.h"
#include "files.h"
#include "zip.h"
#include "hw.h"
#include "console.h"
#include "dlog.h"
#include "raster.h"
#include "blit.h"
#include "screen.h"
#include "slot.h"
#include "track.h"
#include "track_build.h"
#include "road.h"
#include "s2car.h"
#include "race.h"
#include "s2teams.h"
#include "s2sound.h"
#include "s2menu.h"
#include "s2gfx.h"
#include "s2load.h"
#include "s2pause.h"
#include "s2end.h"
#include "s2save.h"
#include "music.h"
#include "s2hud.h"
#include "prof.h"
#include <string.h>

/* The SDRAM (docs/soc.md): the track image where Screamer 1's was, the game's containers above it (the
   ZIP's entries read whole), the build's scratch up to the program */
#define TEX_FILE    ((const volatile uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2100000))   /* the image, up to 7 MB */
#define TEX_MAX     0x700000u
#define GFX_MEM     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2800000))                  /* GFX/MAPnH01.DAT */
#define GFX_MAX     0x400000u
#define CARS_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2C00000))                  /* the place for the cars' ZIP entries (s2teams.h) */
#define CARS_MAX    0x100000u
#define DATA_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D00000))                  /* DATA/MAPn.DAT */
#define DATA_MAX    0x40000u
#define ENV_MEM     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D40000))                  /* GFX/ENVPn01.DAT (up to 20 KB) */
#define ENV_MAX     0x20000u
#define BLOB_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))                  /* the grid's cars, unpacked */
#define BLOB_ROOM   (RACE_CARS * S2_CAR_ROOM)                                         /* 0xE0000 */
#define SCRATCH_MEM ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000 + BLOB_ROOM))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000u + BLOB_ROOM))

/* the simulation's own slots (core/data.json does not have them; tools/sim.sh): a finished image offered
   instead of the build, the frames to run, the track */
#define IMAGE_SLOT  30
#define RACE_SLOT   29
#define TRACK_SLOT  28

#define TRACKS      7
#ifndef VIEWER_TIMELINE
#define VIEWER_TIMELINE 0      /* 1: the debug log also gets the frame timeline at 5 s (a kilobyte of the log's 4) */
#endif
#define SPEED       9265.0f             /* 150 km/h in units a second (drive_kmh: speed x 340 / 21000) */
#define STEP_S      0.02f               /* a video frame */
#define MS_CYCLES   (CPU_HZ / 1000)
#define LATE_CYCLES (CPU_HZ / 50)       /* a frame over 20 ms misses its vertical blanking */

/* The driving mode is a race (race.h, docs/development.md): the physics of the four cars steps once per video frame
   since the last picture, at most as many as Screamer 1's race takes (main.c); a car with no drivable ground under it
   for 3 s, or this far below the lowest road point of the track, goes back onto the line (race.c) */
#define DRIVE_MAX_STEPS 4
#ifndef RACE_OVERLAP
#define RACE_OVERLAP    2               /* the computers' cars' steps: 2 while the pieces are handed on, whenever the geometry unit's buffer is full, the
                                           rest after the last piece and while the rasterizer finishes the picture (race.h); 1 only after the last
                                           piece; 0 before the scene (one step with the player's) */
#endif
#define FALL_UNITS      3000.0f
#define OFFER_FRAMES    50              /* the finished race offers "A: race again, B: menu" a second after the line */

static const struct rs_clip clip = { 0, 0, FB_W, FB_H };
static struct blit_list back;

/* the screen's palette for the loading and error screens: entries 0 to 4 (files.c's) */
#define BLACK   0
#define WHITE   1
#define GREEN   2
#define RED     3
#define GREY    4

static int ui_ready;                    /* the 2D toolkit (s2gfx.h) is up: the loading screen and the pause menu */
static uint32_t sim_frames;             /* in the simulation: the frames to draw before it ends */
static uint32_t results;
static int sim;

/* "10.4" for cycles, to one decimal of a millisecond */
static char *fmt_ms(char *p, uint32_t cycles)
{
    uint32_t t = (cycles + 3712) / 7425;
    p = fmt_udec(p, t / 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    *p = 0;
    return p;
}

static void screen_text(int n, const char *l1, const char *l2, const char *l3, int err)
{
    static const uint32_t pal[5] = { 0x000000, 0xFFFFFF, 0x40D070, 0xF04848, 0x909090 };
    uint32_t st = wait_swap_done();
    volatile uint8_t *fb = FB(!(st & VID_FRONT));
    for (int i = 0; i < 5; i++) VID_PAL[i] = pal[i];
    fb_fill(fb, 0, 0, FB_W, FB_H, BLACK);
    fb_fill(fb, 0, 0, FB_W, 8, WHITE);
    con_text(fb, 1, 0, "SCREAMER 2 - TRACK VIEWER", BLACK, WHITE);
    char line[32];
    fmt_udec(fmt_str(line, "TRACK "), (uint32_t)n);
    con_text(fb, 14, 10, line, err ? RED : WHITE, BLACK);
    if (l1) con_text(fb, 20 - (int)__builtin_strlen(l1) / 2, 12, l1, err ? RED : GREEN, BLACK);
    if (l2) con_text(fb, 20 - (int)__builtin_strlen(l2) / 2, 13, l2, WHITE, BLACK);
    if (l3) con_text(fb, 20 - (int)__builtin_strlen(l3) / 2, 14, l3, GREY, BLACK);
    con_text(fb, 1, 15, "RIDE   d-pad left/right: track", GREY, BLACK);
    con_text(fb, 1, 16, "       A: cars 4/0/1  B: timing", GREY, BLACK);
    con_text(fb, 1, 17, "       Start: drive  Select+B: menu", GREY, BLACK);
    con_text(fb, 1, 19, "DRIVE  d-pad: steer  B gas  Y brake", GREY, BLACK);
    con_text(fb, 1, 20, "       A handbrake  L/R gears", GREY, BLACK);
    con_text(fb, 1, 21, "       up: auto/manual  X: timing", GREY, BLACK);
    con_text(fb, 1, 22, "       Select + left/right: track", GREY, BLACK);
    con_text(fb, 1, 23, "       Select + Start: restart", GREY, BLACK);
    con_text(fb, 1, 24, "       Start: pause menu (exit there)", GREY, BLACK);
    VID_SWAP = 1;
}

/* a ZIP entry whole to dest; 1 if that worked, then its size and the cycles it took are added to *size
   and *cycles. A failure goes into the log at once (the log has 4 KB: a track's loading is one line) */
static int read_entry(struct zip *z, const char *path, uint8_t *dest, uint32_t max, uint32_t *size, uint32_t *cycles)
{
    const struct zip_entry *e = zip_find(z, path);
    if (!e) {
        dlog_str("viewer: ");
        dlog_str(path);
        dlog_str(" is not in the ZIP\n");
        return 0;
    }
    uint32_t t0 = rdcycle();
    int r = zip_read(z, e, dest, max);
    if (r) {
        dlog_str("viewer: ");
        dlog_str(path);
        dlog_str(": read failed (zip result ");
        dlog_dec(r);
        dlog_str(")\n");
        return 0;
    }
    *size = e->size;
    *cycles += rdcycle() - t0;
    return 1;
}

/* the simulation's short cut (tools/simcheck.sh, SIM_IMAGE): the finished image as data slot IMAGE_SLOT,
   which core/data.json does not have; its size is 0 on the Pocket */
static int sim_image(void)
{
    uint32_t size = slot_size(IMAGE_SLOT), body = size & ~3u;
    if (!size || size > TEX_MAX) return 0;
    if (body && slot_read(IMAGE_SLOT, 0, (uint32_t)(uintptr_t)TEX_FILE, body)) return 0;
    if (size > body && slot_read(IMAGE_SLOT, body, (uint32_t)(uintptr_t)TEX_FILE + body, SLOT_REST)) return 0;
    return track_open(TEX_FILE);
}

/* Track n: its containers from the ZIP, the image built, checked and set up, the road chained. 1 if all
   worked, else 0 and what stopped it in *what. The grid's cars are read and unpacked once (s2teams.h). The log
   gets one line: the bytes read and the milliseconds of each step. */
static int load_track(struct zip *z, int n, const char **what)
{
    static struct s2_files f;
    static char gfx_name[24], data_name[24], env_name[24];
    struct track_build_info info = { "", 0, 0, 0 };
    uint32_t t0 = rdcycle(), read_cycles = 0, read_bytes = 0, road_cycles = 0, image = 0;
    if (sim && slot_size(IMAGE_SLOT)) {
        if (!sim_image()) return *what = "the image", 0;
    } else {
        uint32_t size = 0;
        fmt_str(fmt_udec(fmt_str(gfx_name, "GFX/MAP"), (uint32_t)n), "H01.DAT");
        fmt_str(fmt_udec(fmt_str(data_name, "DATA/MAP"), (uint32_t)n), ".DAT");
        fmt_str(fmt_udec(fmt_str(env_name, "GFX/ENVP"), (uint32_t)n), "01.DAT");
        uint32_t cars_t0 = rdcycle();
        f.car = s2_cars_load(z, race_grid, RACE_CARS, 0, CARS_MEM, CARS_MAX, BLOB_MEM, &read_bytes, what);
        if (!f.car) return 0;
        f.ncars = RACE_CARS;
        read_cycles += rdcycle() - cars_t0;              /* (the first load: with the cars' unpacking) */
        f.gfx = GFX_MEM;
        f.data = DATA_MEM;
        if (!read_entry(z, gfx_name, GFX_MEM, GFX_MAX, &f.gfx_size, &read_cycles)) return *what = "the track's GFX file", 0;
        if (!read_entry(z, data_name, DATA_MEM, DATA_MAX, &f.data_size, &read_cycles)) return *what = "the track's DATA file", 0;
        read_bytes += f.gfx_size + f.data_size;
        f.env = ENV_MEM;                            /* the windows' reflection: without it the glass keeps the cars' placeholder */
        if (!read_entry(z, env_name, ENV_MEM, ENV_MAX, &f.env_size, &read_cycles)) f.env = 0;
        else read_bytes += f.env_size;
        size = track_build_s2(&f, n, (uint8_t *)(uintptr_t)TEX_FILE, TEX_MAX, SCRATCH_MEM, SCRATCH_MAX, &info);
        while (!(MEM_STATUS & MEM_WRITES_IDLE))     /* all of it in the SDRAM for the 3D units */
            ;
        dcache_invalidate();                        /* the CPU reads the image back (track_init) */
        if (!size || !track_open(TEX_FILE)) {
            dlog_str("viewer: image not built, stopped at ");
            dlog_str(size ? "its header" : info.fail);
            dlog_char('\n');
            *what = size ? "the image header" : "the image build";
            return 0;
        }
        image = size;
    }
    /* (no check of the image's block CRC-32s here: the native check of the build has them, tools/s2/check_track_build_s2.sh) */
    int placements = track_init();
    if (!placements) return *what = "the track limits", 0;
    uint32_t t2 = rdcycle();
    int points = road_build();
    road_cycles = rdcycle() - t2;
    if (!points) return *what = "the road", 0;
    dlog_str("T");
    dlog_dec(n);
    dlog_str(" load ");
    dlog_dec((int32_t)((rdcycle() - t0) / MS_CYCLES));
    dlog_str(" ms: read ");
    dlog_dec((int32_t)(read_bytes / 1024));
    dlog_str(" KB ");
    dlog_dec((int32_t)(read_cycles / MS_CYCLES));
    dlog_str(", build ");
    dlog_dec((int32_t)(image / 1024));
    dlog_str(" KB ");
    dlog_dec((int32_t)(info.cycles / MS_CYCLES));
    dlog_str(" (unpack ");
    dlog_dec((int32_t)(info.unpack_cycles / MS_CYCLES));
    dlog_str("), road ");
    dlog_dec(points);
    dlog_str(" pts ");
    dlog_dec((int32_t)(road_cycles / MS_CYCLES));
    dlog_str(", ");
    dlog_dec(placements);
    dlog_str(" pl\n");
    return 1;
}

/* the frames of a visit (a track with a number of cars): what the overlay, the log and debug.txt's first line show */
struct stats {
    uint64_t sum;                       /* cycles of all the frames' work */
    uint32_t n, max, over;              /* frames, the longest, the ones over 20 ms */
    uint32_t faces, pieces;             /* sums over the frames */
    float worst_s;                      /* where the longest frame was: units along the road (the race: the player's node of the line) */
    uint32_t w_pre, w_cull, w_scene, w_others, w_wait, w_faces, w_pieces;   /* and what it was made of: cycles before the scene, in the cull,
                                                                               in the pieces' loop, in the computers' steps, waiting for the rasterizer */
    uint32_t video;                     /* video frames (1/50 s) since the first frame */
    uint64_t phys, others;              /* the driving mode: cycles before the scene (the player's step) and of the computers' steps after it, summed */
    uint32_t phys_max, others_max;      /* and the longest of each */
};

/* cars < 0: the driving mode (no cars ahead, no place on the road) */
static void stats_text(char *p, int n, int cars, const struct stats *s, int full)
{
    uint32_t dvf = s->video, fps10 = dvf && s->n > 1 ? (s->n - 1) * 500 / dvf : 0;   /* n - 1 intervals */
    char *q = fmt_udec(fmt_str(p, full ? "T" : "Screamer Pocket T"), (uint32_t)n);
    q = cars < 0 ? fmt_str(q, " drv") : fmt_udec(fmt_str(q, " c"), (uint32_t)cars);
    q = fmt_udec(fmt_str(q, full ? " " : " fr"), s->n);
    q = fmt_str(q, full ? "fr avg" : " avg");
    q = fmt_ms(q, s->n ? (uint32_t)(s->sum / s->n) : 0);
    q = fmt_ms(fmt_str(q, " max"), s->max);
    if (full && cars >= 0) q = fmt_str(fmt_udec(fmt_str(q, "@"), (uint32_t)(s->worst_s / 1000.0f)), "k");
    q = fmt_udec(fmt_str(q, " late"), s->over);
    q = fmt_udec(fmt_str(q, full ? " " : " fps"), fps10 / 10);
    *q++ = '.';
    *q++ = (char)('0' + fps10 % 10);
    *q = 0;
    if (full) {
        q = fmt_udec(fmt_str(q, "/s f"), s->n ? s->faces / s->n : 0);
        q = fmt_udec(fmt_str(q, " p"), s->n ? s->pieces / s->n : 0);
        if (cars < 0) {                                 /* the race: mean and worst ms of the player's step and the scene set-up (ph), of the computers' steps (o) */
            q = fmt_ms(fmt_str(q, " ph"), s->n ? (uint32_t)(s->phys / s->n) : 0);
            q = fmt_ms(fmt_str(q, "/"), s->phys_max);
            q = fmt_ms(fmt_str(q, " o"), s->n ? (uint32_t)(s->others / s->n) : 0);
            q = fmt_ms(fmt_str(q, "/"), s->others_max);
        }
    }
    if (s->n) {                                         /* the longest frame: its parts in ms, its faces and pieces, where the player was */
        q = fmt_ms(fmt_str(q, " | a"), s->w_pre);
        q = fmt_ms(fmt_str(q, " c"), s->w_cull);
        q = fmt_ms(fmt_str(q, " s"), s->w_scene);
        q = fmt_ms(fmt_str(q, " o"), s->w_others);
        q = fmt_ms(fmt_str(q, " w"), s->w_wait);
        q = fmt_udec(fmt_str(q, " f"), s->w_faces);
        q = fmt_udec(fmt_str(q, " p"), s->w_pieces);
        if (cars < 0) q = fmt_udec(fmt_str(q, " n"), (uint32_t)s->worst_s);
    }
    *q = 0;
}

/* a line into the log: track, cars (or drv), frames, mean and worst ms (@ where on the road, in thousand units),
   frames over 20 ms, pictures a second, faces and pieces a frame */
static void log_stats(int n, int cars, const struct stats *s)
{
    char buf[160];
    stats_text(buf, n, cars, s, 1);
    dlog_str(buf);
    dlog_char('\n');
}

/* a visit ends (another track, the cars, the mode): its one line into the log, unless it was a moment. The
   log has 4 KB and no ring: the first line is the live statistics, so a visit that ends with the core
   being quit needs no line. */
static void leave(int n, int cars, const struct stats *s)
{
    s2snd_stop();                       /* the race's sound, before a track's load takes the data slot */
    if (s->n >= 50) log_stats(n, cars, s);
}

/* the race of the driving mode (race.h): the player's car is car 0; the gearbox the player chose stays through new
   races and tracks */
static int manual;

/* a new race on track n: the four cars on the grid, the countdown from 3 s; 0 if the line or a car could not be set up */
static int race_start(int n, float floor_z)
{
    uint32_t t0 = rdcycle();
    int ok = race_init(n, floor_z);
    race_car(0)->manual = manual;
    static int logged;                                  /* the log has 4 KB: the first start of the run tells how long it took */
    if (!ok) dlog_str("viewer: race_init failed\n");
    else if (!logged++) {
        dlog_str("race init ");
        dlog_dec((int32_t)((rdcycle() - t0) / MS_CYCLES));
        dlog_str(" ms\n");
    }
    if (ok) s2snd_race_start(n);                        /* (after the timing: the music's first fill takes 170 ms) */
    if (ok) s2hud_begin(n);
    return ok;
}

/* the controls of the driving mode from the buttons, as Screamer 1 reads them (car_keys: B the gas, Y the brake,
   the d-pad the steering, held; L and R the gears on the step they go down). Select held (sel) makes the d-pad a
   track switch, so it does not steer. */
static struct s2car_input car_input(uint32_t keys, int sel)
{
    return (struct s2car_input){ .throttle = (keys & KEY_B) ? 1.0f : 0.0f, .brake = (keys & KEY_Y) ? 1.0f : 0.0f,
                                 .steer = sel ? 0.0f : ((keys & KEY_RIGHT) ? 1.0f : 0.0f) - ((keys & KEY_LEFT) ? 1.0f : 0.0f),
                                 .handbrake = (keys & KEY_A) != 0 };
}

/* In the simulation the race runs on scripted buttons, one physics step (20 ms) per frame: the gas all the time (the
   cars are held for the first 3 s, 150 steps), nothing else. */
static uint32_t sim_keys(uint32_t step)
{
    (void)step;
    return KEY_B;
}

/* "01:23:45" for seconds: minutes, seconds, hundredths */
static char *fmt_time(char *p, float seconds)
{
    uint32_t t = seconds > 0.0f ? (uint32_t)(seconds * 100.0f) : 0, v[3] = { t / 6000 % 100, t / 100 % 60, t % 100 };
    for (int k = 0; k < 3; k++) {
        if (k) *p++ = ':';
        *p++ = (char)('0' + v[k] / 10);
        *p++ = (char)('0' + v[k] % 10);
    }
    *p = 0;
    return p;
}

/* the computers' steps in the gaps of the scene's loop (frame.h hw_idle): one slice of work each time the geometry unit's buffer is full */
static uint32_t others_in_scene;

static void others_slice(void)
{
    uint32_t t = rdcycle();
    race_others_slice();
    others_in_scene += rdcycle() - t;
}

/* the race's text over the picture: the start lights, laps, place and times (hud: s2hud.c draws those); when the player has finished, the
   result and, once offer is set (a second after the finish, so that the gas button held across the line does nothing), the two buttons: A
   the race again, B the menu. Returns 1 if the player has finished. */
static int race_text(volatile uint8_t *fb, uint8_t fg, uint8_t bg, int offer, int hud)
{
    struct race_hud h;
    char l[40], *p;
    race_get_hud(&h);
    static const char *const ord[5] = { "", "1ST", "2ND", "3RD", "4TH" };
    if (!hud && h.count >= 0) {                         /* 3, 2, 1, GO: big, with a shadow */
        const char *t = h.count == 3 ? "3" : h.count == 2 ? "2" : h.count == 1 ? "1" : "GO!";
        int x = 160 - (int)__builtin_strlen(t) * 24;
        con_big(fb, x + 3, 43, 6, t, bg);
        con_big(fb, x, 40, 6, t, fg);
    }
    if (!hud) {
        p = fmt_udec(fmt_str(l, "LAP "), (uint32_t)h.lap);
        fmt_udec(fmt_str(p, "/"), (uint32_t)h.laps);
        con_text(fb, CON_COLS - (int)__builtin_strlen(l), 1, l, fg, bg);
        p = fmt_udec(fmt_str(l, "POS "), (uint32_t)h.place);
        fmt_udec(fmt_str(p, "/"), (uint32_t)h.cars);
        con_text(fb, CON_COLS - (int)__builtin_strlen(l), 2, l, fg, bg);
        fmt_time(fmt_str(l, "TIME "), h.time);
        con_text(fb, 0, 3, l, fg, bg);
        fmt_time(fmt_str(l, "LAP  "), h.lap_time);
        con_text(fb, 0, 4, l, fg, bg);
        if (h.best_lap >= 0.0f) {
            fmt_time(fmt_str(l, "BEST "), h.best_lap);
            con_text(fb, 0, 5, l, fg, bg);
        }
    }
    if (h.finished && !ui_ready) {                      /* the end without the toolkit's screen (s2end.h): the place, the time */
        con_big(fb, 160 - 3 * 32 + 3, 53, 4, "FINISH", bg);
        con_big(fb, 160 - 3 * 32, 50, 4, "FINISH", fg);
        p = fmt_str(l, ord[h.place < 5 ? h.place : 4]);
        p = fmt_str(p, " OF ");
        fmt_udec(p, (uint32_t)h.cars);
        con_text(fb, 20 - (int)__builtin_strlen(l) / 2, 11, l, fg, bg);
        fmt_time(fmt_str(l, "TIME "), h.time);
        con_text(fb, 20 - (int)__builtin_strlen(l) / 2, 12, l, fg, bg);
        if (offer) con_text(fb, 20 - 12, 14, " A: race again   B: menu ", fg, bg);
    }
    return h.finished;
}

/* what the end screen asked for (s2end.h): S2END_AGAIN, S2END_EXIT, or none */
static int end_choice;

/* the end of the race over the picture (the original's, s2end.h) once the player has finished; the race's own text is under it */
static void end_screen(int n, volatile uint8_t *fb, const uint32_t *pal, uint32_t fin_frames, uint32_t vf, uint32_t press)
{
    struct race_hud h;
    race_get_hud(&h);
    struct s2_end_in in = { n, race_laps, h.place, h.time, h.best_lap, race_grid[0].team, race_grid[0].car };
    end_choice = s2end_frame(fb, (int)draw_buf, pal, &in, fin_frames, vf, press);
    (void)fb;
}

static int other_track(int n, uint32_t press)
{
    return (press & KEY_RIGHT) ? n % TRACKS + 1 : (n + TRACKS - 2) % TRACKS + 1;
}

/* A visit to track n until the player picks another (its number), asks for the menu (0: Select + B, or B after the finish), or a
   button changes the cars, the mode or the overlay (they stay: -n, the same track again). The image is set up (load_track).
   Ride mode (*drive 0): the camera rides the road at a constant speed, the player's stand-in car and up to
   three more ahead of it. Driving mode: a race (race.h): the player drives car 0 on the physics (s2car.h), three computer
   cars on the grid with him, the camera is the physics' chase camera. */
static int visit(int n, int *cars, int *overlay, int *drive)
{
    const uint32_t *pal = track_palette();
    const uint8_t col_text = nearest((const volatile uint32_t *)pal, 255, 255, 255);
    const uint8_t col_bg = nearest((const volatile uint32_t *)pal, 0, 0, 0);
    track_mode = TRACK_HW;
    track_hw_piece = hw_piece;
    GU_MLB = (uint32_t)((uintptr_t)track_mlb() - SDRAM_BASE);
    GU_INVAL = 1;
    RS_CTRL = 1;
    static float resume_s;                              /* where the road was left when only the cars changed */
    static int timeline_done;
    uint32_t keys_before = in_keys(), last_vf = 0, drawn = 0, window_vf = 0, fin_frames = 0;    /* (frames the player has been finished) */
    struct stats st_all = { 0 };
    float s = resume_s, floor_z = road_floor() - FALL_UNITS;
    resume_s = 0.0f;
    uint32_t win_n = 0, win_sum = 0, win_max = 0, shown_mean = 0, shown_max = 0, shown_faces = 0;
    if (*drive && !race_start(n, floor_z)) {          /* no race on this track: the ride instead */
        *drive = 0;
        return -n;
    }
    s2load_fade();                                      /* the LOADING picture was up through the track's build and the race's set-up (no-op if none) */
    for (;;) {
        uint32_t st = wait_swap_done(), t_frame = rdcycle(), vf = VID_FRAME(st);
        uint32_t keys = in_keys(), press = keys & ~keys_before;
        keys_before = keys;
        int sel = (keys & KEY_SELECT) != 0, mode = *drive ? -1 : *cars;
        tl_begin(t_frame);
        frames_shown = drawn;                           /* every frame drawn so far has been swapped in */
        if (sim && drawn == sim_frames) {               /* the simulation ends: the last frame is up */
            SYS_SIM = SIM_DONE | results;
            dlog_str("SYS_SIM=");
            dlog_hex(SIM_DONE | results);
            dlog_char('\n');
        }
        uint32_t dvf = drawn ? (vf - last_vf) & 0xFFFF : 0;     /* video frames since the last frame started */
        if (drawn == 0) window_vf = vf;
        st_all.video += dvf;
        last_vf = vf;
        /* the buttons: another track (this visit ends), the cars, the mode, the overlay */
        if (*drive) {
            if (sel && (press & (KEY_LEFT | KEY_RIGHT))) {
                leave(n, mode, &st_all);
                return other_track(n, press);
            }
            if (!ui_ready && sel && (press & KEY_B)) {  /* Select + B: back to the menu, where there is no pause menu */
                leave(n, mode, &st_all);
                return 0;
            }
            if (end_choice == S2END_EXIT) {             /* the end screen's choices */
                leave(n, mode, &st_all);
                end_choice = 0;
                return 0;
            }
            if (end_choice == S2END_AGAIN) {
                end_choice = 0;
                race_start(n, floor_z);
                fin_frames = 0;
            }
            if (!ui_ready && fin_frames > OFFER_FRAMES) {   /* without the toolkit, after the finish: A the race again, B the menu */
                if (press & KEY_B) {
                    leave(n, mode, &st_all);
                    return 0;
                }
                if (press & KEY_A) {
                    race_start(n, floor_z);
                    fin_frames = 0;
                }
            }
            if (press & KEY_START) {
                if (sel) {                              /* Select + Start: the race again from the grid */
                    race_start(n, floor_z);
                    fin_frames = 0;
                } else if (ui_ready && drawn && !fin_frames) {      /* Start: the pause menu (not once the player has finished) */
                    int choice = s2pause_run(!!(st & VID_FRONT), pal);
                    if (choice == S2PAUSE_EXIT) {
                        leave(n, mode, &st_all);
                        return 0;
                    }
                    if (choice == S2PAUSE_RESTART) {
                        race_start(n, floor_z);
                        fin_frames = 0;
                    }
                    keys_before = in_keys();            /* (what is held now, the gas, is no press) */
                    window_vf = last_vf = VID_FRAME(VID_STATUS);    /* the pause is not racing time: the next frame is one step */
                    continue;
                }
            }
            if (press & KEY_X) *overlay = !*overlay;
            if (press & KEY_UP) race_car(0)->manual = manual = !manual;
        } else {
            if (press & (KEY_LEFT | KEY_RIGHT)) {
                leave(n, mode, &st_all);
                return other_track(n, press);
            }
            if (press & KEY_A) {
                leave(n, mode, &st_all);
                *cars = *cars == 4 ? 0 : *cars == 0 ? 1 : 4;
                resume_s = s;
                return -n;                              /* the same track, the statistics from the start */
            }
            if (press & KEY_START) {
                leave(n, mode, &st_all);
                *drive = 1;
                return -n;
            }
            if (press & KEY_B) {
                if (sel) {                              /* Select + B: back to the menu */
                    leave(n, mode, &st_all);
                    return 0;
                }
                *overlay = !*overlay;
            }
        }

        /* the scene moves with real time: a video frame is 1/50 s whatever the frame took */
        struct track_cam cam;
        static struct track_object objs[ROAD_CARS];
        int nobj;
        uint32_t race_pending = 0, others_cycles = 0;       /* steps the computers' cars still have to take, what they took */
        uint32_t hud_steps = 1;                             /* the 20 ms steps this frame took, for the race screen's timers */
        others_in_scene = 0;
        if (*drive) {
            /* the physics at 50 steps a second, one per video frame since the last picture (in the simulation, one a
               frame and the scripted buttons), at most DRIVE_MAX_STEPS as Screamer 1's race does */
            uint32_t steps = sim || !drawn ? 1 : dvf > DRIVE_MAX_STEPS ? DRIVE_MAX_STEPS : dvf;
            for (uint32_t k = 0; k < steps; k++) {
                struct s2car_input in = car_input(sim ? sim_keys(drawn) : keys, sel);
                in.gear_up = k == 0 && (press & KEY_R);         /* a gear on the one step the button went down */
                in.gear_down = k == 0 && (press & KEY_L);
                race_step_player(&in);
                if (!RACE_OVERLAP) race_step_others(1);
            }
            race_pending = RACE_OVERLAP ? steps : 0;
            hud_steps = steps;
            if (race_pending && RACE_OVERLAP > 1) race_others_begin(race_pending);
            struct s2car_state *rc[RACE_CARS];
            for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
            PF_VAR(pf);
            nobj = road_race_scene(rc, race_cars(), &cam, objs);
            PF_ADD(PF_SCENE, pf); PF_SET(pf);
            s2snd_frame(&cam);                          /* the race's sound (s2sound.h) */
            PF_ADD(PF_SOUND, pf);
        } else {
            uint32_t steps = drawn ? (dvf > 10 ? 10 : dvf) : 1;
            s += (float)steps * STEP_S * SPEED;
            if (s > road_length()) s -= road_length();
            nobj = road_scene(s, *cars, &cam, objs);
        }
        tl_mark(TL_PHYS);
        const uint32_t phys_cycles = rdcycle() - t_frame;
        struct su_view view;
        track_view(&clip, &view);

        /* the backdrop (the game's 0x83a68): the blitter copies it while the CPU culls the scene; the first
           piece for the geometry unit waits until it is done (hw_piece) */
        volatile uint8_t *fb = FB(!(st & VID_FRONT));
        draw_buf = !(st & VID_FRONT);
        blit_start(&back, (int)draw_buf);
        track_backdrop_blit(&back, &clip, &cam);
        BL_CLOCKS = 0;
        back_clocks = back_cycles = 0;
        back_wait = 1;
        hw_blit(&back);
        pal_faded(pal, 0x8000);

        /* the pieces from the CPU to the geometry unit, its faces to the setup unit, the triangles to the
           rasterizer */
        struct track_stats ts;
        RS_PIXELS = 0;
        RS_MISSES = 0;
        RS_CLOCKS = 0;
        SU_CLOCKS = 0;
        GU_CLOCKS = 0;
        GU_FACES = 0;
        GU_MODE = draw_buf << 8;
        draw_wait = 0;
        tl_mark(TL_PREP);
        RS_CLIP0 = RS_XY(clip.x0, clip.y0);
        RS_CLIP1 = RS_XY(clip.x1, clip.y1);
        su_view_regs(&view);
        tl_scene = 1;
        const uint32_t t_scene = rdcycle();
        if (race_pending && RACE_OVERLAP > 1) hw_idle = others_slice;
        track_draw_cam(0, &clip, &cam, objs, nobj, &ts);
        hw_idle = 0;
        tl_scene = 0;
        tl_mark(TL_SCENE);
        /* the race screen (s2hud.c): its blitter list is made and handed on now, the blitter draws it over the finished scene as soon as
           the 3D units are done, while the CPU goes on with the computers' steps; the timing overlay's two lines come after it, and
           the screen's elements near them move down 16 rows while the overlay is on */
        const int hud = *drive && s2hud_ready();
        if (hud) s2hud_draw(&cam, &view, hud_steps, *overlay ? 16 : 0);
        if (*drive) {                                   /* the computers' cars' steps (what is left of them) while the rasterizer finishes the picture */
            uint32_t t_o = rdcycle();
            if (RACE_OVERLAP > 1) while (race_others_slice())
                ;
            else race_step_others(race_pending);
            others_cycles = rdcycle() - t_o + others_in_scene;
            tl_mark(TL_OTHERS);                         /* (the timeline: the computers' steps after the scene, not a wait) */
        }
        const uint32_t t_wait = rdcycle();
        while (RS_DRAW & RS_BUSY)
            ;
        tl_mark(TL_WAIT);
        uint32_t work = rdcycle() - t_frame, faces = GU_FACES;

        /* the timing, drawn over the picture once the rasterizer and the blitter are done; in the driving mode without the race screen also the
           speed and the gear (top right) */
        if (*overlay) {
            char l[40], *p;
            p = fmt_ms(l, shown_mean);
            p = fmt_ms(fmt_str(p, " ms  max "), shown_max);
            fmt_str(p, " ms");
            con_text(fb, 0, 0, l, col_text, col_bg);
            p = fmt_udec(fmt_str(l, "T"), (uint32_t)n);
            p = *drive ? fmt_str(p, "  drive") : fmt_udec(fmt_str(p, "  cars "), (uint32_t)*cars);
            fmt_udec(fmt_str(p, "  faces "), shown_faces);
            con_text(fb, 0, 1, l, col_text, col_bg);
        }
        if (*drive) {
            const struct s2car_state *pc = race_car(0);
            char l[24], *p = fmt_udec(l, (uint32_t)(pc->speed_mph + 0.5f));
            p = fmt_str(p, " mph  G");
            p = pc->gear > 0 ? fmt_udec(p, (uint32_t)pc->gear) : fmt_str(p, "N");
            fmt_str(p, manual ? " MAN" : " AUTO");
            if (!hud) con_text(fb, CON_COLS - (int)__builtin_strlen(l), 0, l, col_text, col_bg);
            int finished = race_text(fb, col_text, col_bg, fin_frames > OFFER_FRAMES, hud);
            if (finished && ui_ready) end_screen(n, fb, pal, fin_frames, vf, press);
            fin_frames = finished ? fin_frames + 1 : 0;
#ifdef S2_SIM_END
            if (sim && drawn >= 2 && drawn <= 9) {          /* a measurement build (tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_END=1 TAG=simend): the end screen over the first frames */
                uint32_t t_e = rdcycle();
                end_screen(n, fb, pal, drawn == 2 ? 0 : 1, drawn == 2 ? 1000 : drawn == 3 ? 1200 : 1201 + drawn - 4, 0);
                dlog_str("end screen frame ");
                dlog_dec((int32_t)drawn);
                dlog_str(": ");
                dlog_dec((int32_t)((rdcycle() - t_e) / (CPU_HZ / 1000000)));
                dlog_str(" us\n");
            }
#endif
        }
        tl_mark(TL_HUD);

        /* the statistics of the visit and of the last second */
        st_all.sum += work;
        st_all.n++;
        st_all.faces += faces;
        st_all.phys += phys_cycles;
        if (phys_cycles > st_all.phys_max) st_all.phys_max = phys_cycles;
        st_all.others += others_cycles;
        if (others_cycles > st_all.others_max) st_all.others_max = others_cycles;
        st_all.pieces += ts.pieces;
        st_all.over += work > LATE_CYCLES;
        if (work > st_all.max) {
            st_all.max = work;
            st_all.worst_s = s;
            if (*drive) {
                float node;
                memcpy(&node, &race_car(0)->rec[0x4b8 / 4], 4);       /* where the player is on the line (node, a float) */
                st_all.worst_s = node > 0.0f ? node : 0.0f;
            }
            st_all.w_pre = t_scene - t_frame;
            st_all.w_cull = ts.cyc_cull;
            st_all.w_scene = t_wait - t_scene - others_cycles - ts.cyc_cull;
            st_all.w_others = others_cycles;
            st_all.w_wait = work - (t_wait - t_frame);
            st_all.w_faces = faces;
            st_all.w_pieces = ts.pieces;
        }
        win_n++;
        win_sum += work > 0x20000000u ? 0x20000000u : work;
        if (work > win_max) win_max = work;
        shown_faces = faces;
        if (drawn == 0) shown_mean = shown_max = work;      /* until the first second is full */
        if (((vf - window_vf) & 0xFFFF) >= VID_HZ && win_n) {
            shown_mean = win_sum / win_n;
            shown_max = win_max;
            win_n = win_sum = win_max = 0;
            window_vf = vf;
        }
        if (drawn % 25 == 0) {
            char head[128];
            stats_text(head, n, mode, &st_all, 0);
            dlog_head(head);                            /* debug.txt's first line, live */
#ifndef S2_PROF                                         /* (the profile's log needs the room) */
            if (sim && *drive) {                        /* the driving mode's progress in the simulation */
                dlog_str("d");
                dlog_dec((int32_t)drawn);
                dlog_str(": ");
                dlog_dec((int32_t)(race_car(0)->speed_mph + 0.5f));
                dlog_str(" mph G");
                dlog_dec(race_car(0)->gear);
                dlog_str(" yaw ");
                dlog_dec((int32_t)(race_car(0)->yaw * 100.0f));
                dlog_str(" pos ");
                dlog_dec((int32_t)race_car(0)->pos[0]);
                dlog_char(' ');
                dlog_dec((int32_t)race_car(0)->pos[1]);
                dlog_char('\n');
            }
#endif
        }
        /* the timeline of the frame before, once: it is a kilobyte */
        if (!timeline_done && (sim ? drawn + 1 == sim_frames : VIEWER_TIMELINE && st_all.video >= 5 * VID_HZ)) {
            timeline_done = 1;
            tl_log();
        }
        if (sim && drawn + 1 == sim_frames) {
            log_stats(n, mode, &st_all);
#ifdef S2_PROF
            prof_log(drawn + 1);
#endif
            dlog_str("stack used ");
            dlog_dec((int32_t)stack_used());
            dlog_str(" bytes\n");
        }
        tl_mark(TL_END);
        VID_SWAP = 1;
        drawn++;
    }
}

/* the menu's choices into the race (race.h): the grid, the laps, the computers' level, the gearbox */
static struct s2_menu_cfg cfg;

static void apply_cfg(void)
{
    s2menu_grid(&cfg, race_grid);
    race_laps = s2_menu_laps[cfg.laps];
    race_level = 2 - cfg.skill;
    manual = cfg.manual;
    s2car_damages = s2save_get(S2S_DAMAGE);             /* the other GAME SETTINGS that act in the race (s2pages.h) */
    music_on = s2save_get(S2S_MUSIC) == 0;
}

void __attribute__((noreturn)) viewer_run(uint32_t boot_results)
{
    struct zip *z = files_zip();
    results = boot_results;
    sim = (SYS_FLAGS & FLAG_SIM) != 0;
    sim_frames = sim ? (slot_size(RACE_SLOT) ? slot_size(RACE_SLOT) : 8) : 0;
    int n = sim && slot_size(TRACK_SLOT) ? (int)slot_size(TRACK_SLOT) : 1, cars = 4, overlay = 1;
    int drive = sim && (in_keys() & KEY_START);         /* in the simulation Start held (SIM_KEYS=8000): the driving mode */
    dlog_str("viewer: start\n");
    s2snd_init(z);
    s2menu_init(z);
    s2menu_load(&cfg);
#ifdef S2_SIM_END
    ui_ready = s2gfx_init(z);                           /* (the measurement build of the end screen has the toolkit in the simulation) */
#else
    ui_ready = !sim && s2gfx_init(z);                   /* (the simulation has no menus) */
#endif
    s2hud_init(z);                                      /* the race screen (the simulation has it: its cost is measured there) */
    for (;;) {
        if (!sim) {                                     /* (the simulation takes the track from the testbench and has no menu) */
            int go = s2menu_run(&cfg);
            n = cfg.track;
            drive = go == S2M_RACE;
            apply_cfg();
            s2menu_log(&cfg, go);
        }
        for (;;) {                                      /* the track, its visits, until the player asks for the menu */
            const char *what = "";
            const uint32_t *loading = ui_ready ? s2load_show(n, race_laps) : 0;     /* the original's picture; it stays up while the CPU builds */
            if (!loading) screen_text(n, "Loading...", "the track is built from the ZIP", 0, 0);
            if (load_track(z, n, &what)) {
                int next;
                while ((next = visit(n, &cars, &overlay, &drive)) < 0)
                    ;                                   /* the cars or the mode changed: the same visit again */
                cfg.track = next ? next : n;
                cfg.manual = manual;                    /* (d-pad up in the race) */
                s2menu_store(&cfg);
                if (!next) break;
                n = next;
            } else {                                    /* what stopped it is in the log; left and right try another, B the menu */
                char l[40];
                fmt_str(fmt_str(l, "Could not set up "), what);
                screen_text(n, l, "see debug.txt in the Saves folder", "d-pad left/right: another track  B: menu", 1);
                uint32_t keys_before = in_keys();
                int menu = 0;
                while (!menu) {
                    wait_swap_done();
                    uint32_t keys = in_keys(), press = keys & ~keys_before;
                    keys_before = keys;
                    if (press & KEY_RIGHT) { n = n % TRACKS + 1; break; }
                    if (press & KEY_LEFT) { n = (n + TRACKS - 2) % TRACKS + 1; break; }
                    menu = !sim && (press & KEY_B);
                }
                if (menu) {
                    cfg.track = n;
                    break;
                }
            }
        }
    }
}
