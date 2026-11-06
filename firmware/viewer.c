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
#include "s2anim.h"
#include "s2menu.h"
#include "s2gfx.h"
#include "s2load.h"
#include "s2pause.h"
#include "s2end.h"
#include "s2save.h"
#include "s2league.h"
#include "s2leaguepages.h"
#include "s2desk.h"
#include "s2ghost.h"
#include "s2cam.h"
#include "s2trackpage.h"
#include "s2arcade.h"
#include "s2attract.h"
#include "trackmem.h"
#include "music.h"
#include "s2hud.h"
#include "s2cockpit.h"
#include "s2dash.h"
#include "s2fx.h"
#include "s2props.h"
#include "prof.h"
#include <string.h>

/* The SDRAM (docs/soc.md): the track image where Screamer 1's was, the game's containers above it (the
   ZIP's entries read whole), the build's scratch up to the program: trackmem.h */

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

static struct blit_list back;

/* the screen's palette for the loading and error screens: entries 0 to 4 (files.c's) */
#define BLACK   0
#define WHITE   1
#define GREEN   2
#define RED     3
#define GREY    4

static int ui_ready;                    /* the 2D toolkit (s2gfx.h) is up: the loading screen and the pause menu */
static int league_mode;                 /* the visit is a race of the championship (league_run): no RESTART, no track switch, the end screen is the league's */
static int league_started;              /* ... and the race did start (its cars' places are there to read) */
static int mirror_on;                   /* the rear-view mirror (the original's M: off at the start of every race, not in a replay) */
static int cam_mode;                    /* the camera is s2cam.c's (the trackside cameras after the finish and in the replay, or another car's chase camera), not the player's own */
static int cut_done;                    /* ... and the cut to the trackside cameras at the player's finish has been made in this race */
static struct race_result lg_res[RACE_CARS];    /* the championship's places as the end screen's ENTER took them (a replay does not take them again), or as the race was left */
static int lg_res_valid;
static int attack_mode;                 /* the visit is time attack (1) or the free run (2): attack_run(); no track switch, the end screen is time attack's, a quicker run is kept as the ghost */
#define VISIT_AGAIN 100                 /* visit() ends with the race again asked for on time attack's end screen: the ghost may have changed, attack_race() decides what to load */
static int attract_mode;                /* the visit is the attract sequence (attract_run()): four computer cars, the trackside cameras, the logos and the credits over it, any button ends it */
static struct s2att att;                /* ... its screen's state (s2attract.h) and what it shows this frame */
static struct s2att_view att_view;
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
    con_text(fb, 1, 21, "       X: timing  Select+X: view", GREY, BLACK);
    con_text(fb, 1, 22, "       Select+Y: mirror  Select+<>: track", GREY, BLACK);
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
static int load_cars = RACE_CARS;       /* the cars of the grid the image holds: four, time attack's two (the ghost) or one */
struct viewer_image loaded;      /* what the image holds, so that time attack does not build it again (trackmem.h) */

static int load_track(struct zip *z, int n, const char **what)
{
    loaded.valid = 0;
    static struct s2_files f;
    static char gfx_name[24], data_name[24], env_name[24];
    struct track_build_info info = { "", 0, 0, 0, 0 };
    uint32_t t0 = rdcycle(), read_cycles = 0, read_bytes = 0, road_cycles = 0, image = 0;
    if (sim && slot_size(IMAGE_SLOT)) {
        if (!sim_image()) return *what = "the image", 0;
    } else {
        uint32_t size = 0;
        fmt_str(fmt_udec(fmt_str(gfx_name, "GFX/MAP"), (uint32_t)n), "H01.DAT");
        fmt_str(fmt_udec(fmt_str(data_name, "DATA/MAP"), (uint32_t)n), ".DAT");
        fmt_str(fmt_udec(fmt_str(env_name, "GFX/ENVP"), (uint32_t)n), "01.DAT");
        uint32_t cars_t0 = rdcycle();
        f.car = s2_cars_load(z, race_grid, load_cars, 0, CARS_MEM, CARS_MAX, BLOB_MEM, &read_bytes, what);
        if (!f.car) return 0;
        f.ncars = load_cars;
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
        s2anim_load(n, GFX_MEM, f.gfx_size, DATA_MEM, f.data_size, info.misc_delta, SCRATCH_MEM, SCRATCH_MAX);     /* the animated scenery: rows, paths and meshes, from the containers still there */
        s2props_load(n, GFX_MEM, f.gfx_size, DATA_MEM, f.data_size, info.misc_delta, SCRATCH_MEM, SCRATCH_MAX);     /* the obstacles (cones, fences, chests, crates) and the sparks' meshes */
        while (!(MEM_STATUS & MEM_WRITES_IDLE))     /* (the meshes in the SDRAM for the geometry unit) */
            ;
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
    loaded.valid = 1;
    loaded.track = n;
    loaded.cars = load_cars;
    memcpy(loaded.pick, race_grid, sizeof loaded.pick);
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

/* the race of the driving mode (race.h): the player's car is car 0; the gearbox the player chose (cfg.manual, from the
   TRANSMISSION page or GAME SETTINGS) stays through new races and tracks; nothing in a race changes it */
static int manual;

/* the view a race starts in: the one the player left the last race in (the original keeps it in its options, 0x2b64a, 0x20469, and takes 2, 3, 4 or 6, else 2 at 0x2e205: a new
   save starts in INCAR, the inside view without the dashboard, as a fresh install of the original does: the owner confirmed it, 2026-11-06) */
static int start_view(void)
{
    const int v = s2save_get(S2S_VIEW);
    return v == S2CAM_INCAR || v == S2CAM_BACK || v == S2CAM_NEAR || v == S2CAM_INSIDE ? v : S2CAM_INCAR;
}

/* a view from the car (F1 .. F4), kept for the next race; the race screen follows */
static void set_view(int view)
{
    s2cam_view(view);
    s2hud_view(s2cam_get_view(), s2cam_get_follow());
    if (!race_replaying() && !attract_mode) s2save_set(S2S_VIEW, s2cam_get_view());
}

/* a new race on track n: the four cars on the grid, the countdown from 3 s; 0 if the line or a car could not be set up */
static int race_start(int n, float floor_z)
{
    uint32_t t0 = rdcycle();
    race_car(0)->manual = manual;                       /* (before the cars are set up: the first gear depends on it; the other kinds of race clear it and it is set again) */
    int ok = race_init(n, floor_z);
    road_race_reset();
    if (!attract_mode) race_car(0)->manual = manual;    /* (the attract race has no player: every car's gearbox is the driver's) */
    s2fx_reset();                                       /* (no puffs left from the race before) */
    static int logged;                                  /* the log has 4 KB: the first start of the run tells how long it took */
    if (!ok) dlog_str("viewer: race_init failed\n");
    else if (!logged++) {
        dlog_str("race init ");
        dlog_dec((int32_t)((rdcycle() - t0) / MS_CYCLES));
        dlog_str(" ms\n");
    }
    if (ok) s2anim_begin();                             /* the animated scenery at the start of its paths */
    if (ok) s2props_begin();                            /* the obstacles standing, no sparks */
    s2snd_music_later = attract_mode;                   /* (the attract race: the song begins with its picture, s2att_step()) */
    if (ok) s2snd_race_start(n);                        /* (after the timing: the music's first fill takes 170 ms) */
    if (ok) s2hud_begin(n);
    if (ok) {                                           /* the camera: the player's chase camera, or in a replay the trackside cameras (0x2e1f3: a replay begins in view 1) */
        cam_mode = race_replaying() || attract_mode;    /* (0x2e1f3: a replay and the attract race begin in view 1) */
        cut_done = 0;
        if (!cam_mode) lg_res_valid = 0;
        s2cam_begin(n, cam_mode ? S2CAM_TRACKSIDE : start_view());
        for (int i = 0; i < RACE_CARS; i++) race_car(i)->cam_on = 0;
#ifdef S2_SIM_MIRROR
        mirror_on = 1;                                  /* (the measurement build: tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_MIRROR=1 TAG=mirror runs the simulated race with the mirror on) */
#else
        mirror_on = 0;
#endif
        s2hud_view(s2cam_get_view(), 0);
    }
    return ok;
}

/* the camera follows car `i` (0 .. race_cars() - 1) in the view it has: its chase camera is worked out, the dial is its */
static void follow(int i)
{
    s2cam_follow(i, race_cars());
    for (int k = 0; k < race_cars(); k++) race_car(k)->cam_on = k == s2cam_get_follow();
    s2hud_view(s2cam_get_view(), s2cam_get_follow());
}

/* the same race again from the recording of the one just run (race.h): the trackside cameras, the replay's band's darkening ready */
static int replay_start(int n, float floor_z, const uint32_t *pal)
{
    race_replay = 1;
    const int ok = race_start(n, floor_z);
    if (ok && race_replaying()) {
        s2hud_band_prepare(pal);
        follow(0);
    }
    return ok;
}

/* the replay's band, "TEAM - VIEW" of the car followed and the view (0x4658c: GAME.TXT 40 .. 44 EXTERNAL INCAR BACK NEAR INSIDE, the team's name STRINGS.TXT 5 .. 8) */
static const char *band_text(void)
{
    static char text[40];
    const int view = s2cam_get_view(), car = s2cam_get_follow();
    const char *v = s2gfx_line("EXTERNAL", view == 1 ? 0 : view == 2 ? 1 : view == 3 ? 2 : view == 4 ? 3 : 4, view == 1 ? "EXTERNAL" : "BACK");
    fmt_str(fmt_str(fmt_str(text, s2menu_team_name(race_grid[car].team)), " - "), v);
    return text;
}

/* the controls of the driving mode from the buttons, as Screamer 1 reads them (car_keys: B the gas, Y the brake,
   the d-pad the steering, held; L and R the gears on the step they go down). Select held (sel) makes the d-pad a
   track switch, so it does not steer, and Y the mirror's switch (with X the view's), so it does not brake. */
static struct s2car_input car_input(uint32_t keys, int sel)
{
    return (struct s2car_input){ .throttle = (keys & KEY_B) ? 1.0f : 0.0f, .brake = (keys & KEY_Y) && !sel ? 1.0f : 0.0f,
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

/* The rear-view mirror's pass (STARTL.EXE 0x2b873 draws it after the scene, 0x756b0): the scene again from behind the followed car, 320 x 35 rows with the car and its shadow left out, the
   glass's sense and every block at level of detail 1 or lower (track_draw_mirror); its pixels 2k of each row, right to left, are the 160 x 35 strip the race screen lays at (81, 14). The
   original renders into a buffer of its own; the rasterizer here only draws into the two screen buffers, so the pass goes into the top rows of the one being built (the main scene is drawn
   over it after) and the CPU squeezes the rows out of it as soon as the rasterizer is done. The shadows' and the puffs' blocks are the main pass's too: the objects of the mirror are made
   and drawn first, those of the picture after. */
static uint8_t mirror_strip[S2CK_MIRROR_W * S2CK_MIRROR_ROWS] __attribute__((aligned(4)));

static void draw_mirror(struct s2car_state *const *rc, int n, int follow, const struct track_object *car_objs, int ncar, uint32_t buffer, int overlap)
{
    static struct track_object mobjs[TRACK_MAX_OBJS];
    static struct blit_list mback;
    struct track_cam mc;
    s2cam_mirror((const struct s2car_state *const *)rc, n, &mc);
    int nm = 0;
    for (int k = 0; k < ncar; k++)
        if (k != follow) mobjs[nm++] = car_objs[k];
    nm += s2anim_objects(mobjs + nm, S2ANIM_OBJECTS);
    s2fx_hide_shadow(follow);
    nm += s2fx_objects(&mc, rc, n, mobjs + nm, ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS - nm);
    nm += s2props_objects(&mc, mobjs + nm, ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS + S2PROPS_MAX - nm);
    const struct rs_clip mclip = { 0, 0, FB_W, S2CK_MIRROR_ROWS };
    struct su_view mv;
    track_view_mirror(&mclip, &mv);
    draw_buf = buffer;
    blit_start(&mback, (int)buffer);
    track_backdrop_blit_mirror(&mback, &mclip, &mc);
    while (!(MEM_STATUS & MEM_WRITES_IDLE))                             /* (the panorama turned over, made the first time, is in the SDRAM for the blitter) */
        ;
    BL_CLOCKS = 0;
    back_wait = 1;
    hw_blit(&mback);
    GU_MODE = buffer << 8;
    RS_CLIP0 = RS_XY(mclip.x0, mclip.y0);
    RS_CLIP1 = RS_XY(mclip.x1, mclip.y1);
    su_view_regs(&mv);
    struct track_stats mts;
    if (overlap) hw_idle = others_slice;
    track_draw_mirror(0, &mclip, &mc, mobjs, nm, &mts);
    hw_idle = 0;
    while (overlap && (RS_DRAW & RS_BUSY) && race_others_slice())     /* (the computers' steps fill the wait for the last pixels) */
        ;
    while (RS_DRAW & RS_BUSY)
        ;
    s2ck_glass((volatile uint32_t *)FB(buffer), mirror_strip);
    while (!(MEM_STATUS & MEM_WRITES_IDLE))                             /* (the blitter reads the strip from the SDRAM) */
        ;
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
    struct s2_end_in in = { .track = n, .laps = race_laps, .place = h.place, .time = h.time, .best_lap = h.best_lap, .team = race_grid[0].team,
                            .car = race_grid[0].car, .attack = attack_mode == 1, .replay = race_replay_ok() && !attack_mode, .replaying = race_replaying() };
    if (league_mode) {                                  /* the championship's end lists the cars that have done their laps (s2end.h); a replay's lists them as the race left them (0x48628) */
        struct race_result r[RACE_CARS];
        if (race_replaying() && lg_res_valid) memcpy(r, lg_res, sizeof r);
        else race_get_results(r);
        in.league = 1;
        for (int i = 0; i < RACE_CARS; i++) {
            struct s2_end_car *c = &in.cars[i];
            if (i == 0) {                               /* the player's name as a string: no padding spaces */
                memcpy(c->name, s2save_name(), 3);
                c->name[3] = 0;
                for (int k = 2; k >= 0 && c->name[k] == ' '; k--) c->name[k] = 0;
            } else {
                memcpy(c->name, "CPU", 4);
            }
            c->team = race_grid[i].team;
            c->car = race_grid[i].car;
            c->place = (uint8_t)r[i].place;
            c->done = (uint8_t)r[i].done;
            c->time = r[i].time;
        }
    }
    end_choice = s2end_frame(fb, (int)draw_buf, pal, &in, fin_frames, vf, press);
    (void)fb;
}

/* time attack's player has done his laps and chosen RACE AGAIN or EXIT: his run is the track's ghost if the track has none whole or it is quicker (s2ghost.h) */
static void attack_keep(int n)
{
    if (attack_mode != 1) return;
    struct race_hud h;
    race_get_hud(&h);
    if (!h.finished) return;
    const int kept = s2gh_keep(h.time, h.best_lap, race_laps);
    dlog_str("ghost: T");
    dlog_dec(n);
    dlog_str(kept ? " kept, " : " not kept, ");
    dlog_dec((int32_t)s2gh_rec_steps());
    dlog_str(" steps\n");
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
    struct track_fx fx;
    if (track_fx(&fx)) smg_load(fx.table);              /* the see-through table of the image (smg.h) */
    GU_INVAL = 1;
    RS_CTRL = 1;
    static float resume_s;                              /* where the road was left when only the cars changed */
    static int timeline_done;
    uint32_t keys_before = in_keys(), last_vf = 0, drawn = 0, window_vf = 0, fin_frames = 0;    /* (frames the player has been finished) */
    struct stats st_all = { 0 };
    float s = resume_s, floor_z = road_floor() - FALL_UNITS;
    resume_s = 0.0f;
    uint32_t win_n = 0, win_sum = 0, win_max = 0, shown_mean = 0, shown_max = 0, shown_faces = 0;
    if (*drive && !race_start(n, floor_z)) {          /* no race on this track: the ride instead (the championship stops) */
        *drive = 0;
        return league_mode || attack_mode ? 0 : -n;
    }
    league_started = league_mode;
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
        if (attract_mode) {
            /* (no button but the one that ends it: the screen's step takes it below) */
        } else if (*drive) {
            if (sel && (press & (KEY_LEFT | KEY_RIGHT)) && !league_mode && !attack_mode && !race_replaying()) {
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
                attack_keep(n);
                return 0;
            }
            if (end_choice == S2END_CONTINUE) {         /* the championship's end screen: the race is over, on to the report */
                leave(n, mode, &st_all);
                end_choice = 0;
                if (!lg_res_valid && !race_replaying()) race_get_results(lg_res), lg_res_valid = 1;
                return 0;
            }
            if (end_choice == S2END_REPLAY) {           /* the race again from the recording of this one (the places of the championship's are the ones taken now, 0x47e75) */
                end_choice = 0;
                if (league_mode && !race_replaying() && !lg_res_valid) race_get_results(lg_res), lg_res_valid = 1;
                if (!replay_start(n, floor_z, pal)) {
                    leave(n, mode, &st_all);
                    return 0;
                }
                fin_frames = 0;
            }
            if (end_choice == S2END_AGAIN) {
                end_choice = 0;
                if (attack_mode == 1) {                 /* time attack: the run is kept if it is quicker, and what the next race loads depends on it (the ghost's car) */
                    leave(n, mode, &st_all);
                    attack_keep(n);
                    return VISIT_AGAIN;
                }
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
            if (race_replaying()) {                     /* the replay: there is no pause (ESC ends it, 0x2d7c0), and no controls to give; the views and the car followed are the keys (s2cam.h) */
                struct race_hud hk;
                race_get_hud(&hk);
                if (press & (KEY_START | KEY_A | KEY_SELECT)) race_replay_stop();
                if (!hk.finished) {                     /* (the keys of the views and of the car followed are taken while the player's car is racing, 0x132b4, 0x2d7c0) */
                    if (press & KEY_L) { s2cam_view(S2CAM_TRACKSIDE); follow(s2cam_get_follow()); }
                    if (press & KEY_R) { s2cam_view(S2CAM_BACK); follow(s2cam_get_follow()); }
                    if (press & KEY_B) { s2cam_next_view(); follow(s2cam_get_follow()); }         /* the views from the car in turn, the original's F1 .. F4 (the player's own are not kept: a replay) */
                    if (press & KEY_UP) follow((s2cam_get_follow() + 1) % race_cars());
                }
            } else if (press & KEY_START) {
                if (sel) {                              /* Select + Start: the race again from the grid (the championship's race is run once) */
                    if (!league_mode) {
                        race_start(n, floor_z);
                        fin_frames = 0;
                    }
                } else if (ui_ready && drawn && !fin_frames) {      /* Start: the pause menu (not once the player has finished; the championship's has no RESTART) */
                    int choice = s2pause_run_items(!!(st & VID_FRONT), pal, !league_mode);
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
            if (press & KEY_X) {
                if (sel && !race_replaying() && !cut_done) set_view(s2cam_next_view());           /* Select + X: the next view from the car (the original's F1 .. F4 in turn; not once the player has finished) */
                else if (!sel) *overlay = !*overlay;
            }
            if ((press & KEY_Y) && sel && !race_replaying() && !cut_done && s2cam_get_view() != S2CAM_TRACKSIDE) mirror_on = !mirror_on;      /* Select + Y: the rear-view mirror (the original's M; not in a replay) */
            if (!race_replaying() && race_car(0)->manual != manual) {   /* nothing in a race changes the gearbox (the TRANSMISSION page and GAME SETTINGS choose it; d-pad up used to toggle it and was pressed by mistake while steering): if it differs it is a fault, logged once with the step and put back */
                static int logged;
                if (!logged++) {
                    dlog_str("race: the gearbox changed by itself at step ");
                    dlog_dec((int32_t)race_steps());
                    dlog_str("\n");
                }
                race_car(0)->manual = manual;
            }
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
        static struct track_object objs[ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS + S2PROPS_MAX];     /* the cars, in a race the shadows and the puffs (s2fx.h), then the track's animated scenery (s2anim.h), the obstacles and the sparks (s2props.h) */
        _Static_assert(S2FX_CARS >= RACE_CARS, "s2fx.c keeps state for every car of the race");
        _Static_assert(ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS + S2PROPS_MAX <= TRACK_MAX_OBJS, "track.c draws TRACK_MAX_OBJS objects at most");
        int nobj;
        struct s2car_state *rc[RACE_CARS];                  /* the race's cars, the views from the car (s2cockpit.h): the view, the car followed, the scene's rows, the mirror, and the cars' objects before the car */
        static struct track_object car_objs[ROAD_CARS];     /* followed is taken off the draw list */
        int view_now = S2CAM_BACK, follow_now = 0, rows = FB_H, ncar = 0, mirror_now = 0;
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
                if (attract_mode && !att.over) {                /* the screen of the sequence, a step of it for each step of the race; the cameras' ten seconds */
                    s2cam_attract_tick(race_get_clock(), STEP_S);
                    s2att_step(&att, k == 0 && press != 0, &att_view);
                    if (att_view.music) s2snd_music(n);         /* (in the one step of the sequence that cues it: the frame's last view may not say so) */
                }
                /* the race is the same race whatever the frames took (a replay of it is exact): every step is the player's and then the computers', so that
                   a late frame's extra steps run in that order and only the last step's computers overlap the scene */
                if (!RACE_OVERLAP || k + 1 < steps) race_step_others(1);
            }
            race_pending = RACE_OVERLAP ? 1 : 0;
            hud_steps = steps;
            if (race_pending && RACE_OVERLAP > 1) race_others_begin(race_pending);
            for (int i = 0; i < race_cars(); i++) rc[i] = race_car(i);
            PF_VAR(pf);
            nobj = road_race_scene(rc, race_cars(), &cam, objs);
            view_now = s2cam_get_view();
            follow_now = s2cam_get_follow();
            ncar = nobj;
            memcpy(car_objs, objs, (size_t)ncar * sizeof car_objs[0]);
            if (s2ck_hides_car(view_now) && follow_now < nobj) {      /* INCAR and INSIDE: the car and its shadow are not in the picture (0x2b6b4) */
                memmove(&objs[follow_now], &objs[follow_now + 1], (size_t)(nobj - follow_now - 1) * sizeof objs[0]);
                nobj--;
            }
            rows = s2ck_rows(view_now, s2dash_row(s2ck_model(&race_grid[follow_now])));
            mirror_now = mirror_on && view_now != S2CAM_TRACKSIDE && !race_replaying() && !attract_mode;
            s2anim_frame((float)race_steps() * STEP_S, (int)rc[0]->pos[0], (int)rc[0]->pos[1], (int32_t)rc[0]->rec[0x4A8 / 4]);       /* the race clock is the steps run (0x13335c) */
            nobj += s2anim_objects(objs + nobj, S2ANIM_OBJECTS);
            if (!cut_done && attack_mode != 2) {        /* the player has done his laps: the camera cuts to the trackside cameras, following his car (0x201ee; the free run has no end) */
                struct race_hud hc;
                race_get_hud(&hc);
                if (hc.finished) {
                    cut_done = 1;
                    cam_mode = 1;
                    s2cam_view(S2CAM_TRACKSIDE);
                    follow(0);
                }
            }
            if (cam_mode || view_now != S2CAM_BACK) s2cam_frame((const struct s2car_state *const *)rc, race_cars(), (float)hud_steps * STEP_S, &cam);
            if (mirror_now) draw_mirror(rc, race_cars(), follow_now, car_objs, ncar, !(st & VID_FRONT), race_pending && RACE_OVERLAP > 1);
            s2fx_hide_shadow(s2ck_hides_car(view_now) ? follow_now : -1);
            nobj += s2fx_objects(&cam, rc, race_cars(), objs + nobj, ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS - nobj);       /* (seen from the camera of this frame) */
            nobj += s2props_objects(&cam, objs + nobj, ROAD_CARS + S2FX_MAX + S2ANIM_OBJECTS + S2PROPS_MAX - nobj);        /* (last: should the frame be over the limit, the cars and their effects stay) */
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
        const struct rs_clip clip = { 0, 0, FB_W, rows };
        struct su_view view;
        track_view(&clip, &view);

        /* the backdrop (the game's 0x83a68): the blitter copies it while the CPU culls the scene; the first
           piece for the geometry unit waits until it is done (hw_piece) */
        volatile uint8_t *fb = FB(!(st & VID_FRONT));
        draw_buf = !(st & VID_FRONT);
        const int veiled = attract_mode && (att_view.black || att_view.exit);    /* the sequence's logos and its last frame: the screen is black, nothing of the scene is seen (s2attract.h): not drawn */
        blit_start(&back, (int)draw_buf);
        if (!veiled) track_backdrop_blit(&back, &clip, &cam);
        if (rows < FB_H && !veiled) blit_fill(&back, 0, rows, FB_W, FB_H - rows, 0);       /* (INSIDE: the dashboard's picture is drawn over it, but for the pixels it leaves open) */
        BL_CLOCKS = 0;
        back_clocks = back_cycles = 0;
        back_wait = 1;
        if (!veiled) hw_blit(&back);
        pal_faded(pal, 0x8000);
        if (attract_mode) {                                     /* the picture out of white at the third stage, and into black at the end */
            static uint32_t ap[256];                            /* (not on the stack: the race's 5.2 of 8 KB) */
            if (s2att_palette(pal, &att_view, ap))
                for (int i = 0; i < 256; i++) VID_PAL[i] = ap[i];
        }

        /* the pieces from the CPU to the geometry unit, its faces to the setup unit, the triangles to the
           rasterizer */
        struct track_stats ts;
        if (veiled) memset(&ts, 0, sizeof ts);          /* (nothing drawn: the statistics have nothing) */
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
        if (!veiled) track_draw_cam(0, &clip, &cam, objs, nobj, &ts);
        hw_idle = 0;
        tl_scene = 0;
        tl_mark(TL_SCENE);
        /* the race screen (s2hud.c): its blitter list is made and handed on now, the blitter draws it over the finished scene as soon as
           the 3D units are done, while the CPU goes on with the computers' steps; the timing overlay's two lines come after it, and
           the screen's elements near them move down 16 rows while the overlay is on */
        const int hud = *drive && !attract_mode && s2hud_ready();
        if (hud) {
            s2hud_scene(mirror_now ? mirror_strip : 0, rows);
            s2hud_draw(&cam, &view, hud_steps, *overlay ? 16 : 0);
        }
        if (attract_mode) {                             /* the sequence's screen instead of the race screen (0x45882 calls 0x37ec1 in its place) */
            static struct blit_list att_list;
            blit_start(&att_list, (int)draw_buf);
            s2att_draw(&att_list, &att, &att_view);
            hw_blit(&att_list);
        }
        if (*drive) {                                   /* the computers' cars' steps (what is left of them) while the rasterizer finishes the picture */
            uint32_t t_o = rdcycle();
            if (RACE_OVERLAP > 1) while (race_others_slice())
                ;
            else race_step_others(race_pending);
            others_cycles = rdcycle() - t_o + others_in_scene;
            tl_mark(TL_OTHERS);                         /* (the timeline: the computers' steps after the scene, not a wait) */
            struct s2car_state *fc[RACE_CARS];          /* the puffs of the steps just taken, from the cars as they stand now (s2fx.h; they show in the next frame) */
            for (int i = 0; i < race_cars(); i++) fc[i] = race_car(i);
            s2fx_update(fc, race_cars(), (int)hud_steps, race_steps() * 2, s2cam_get_view() == S2CAM_BACK ? s2cam_get_follow() : -1);
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
        if (*drive && !attract_mode) {
            const struct s2car_state *pc = race_car(0);
            char l[24], *p = fmt_udec(l, (uint32_t)(pc->speed_mph + 0.5f));
            p = fmt_str(p, " mph  G");
            p = pc->gear > 0 ? fmt_udec(p, (uint32_t)pc->gear) : fmt_str(p, "N");
            fmt_str(p, manual ? " MAN" : " AUTO");
            if (!hud) con_text(fb, CON_COLS - (int)__builtin_strlen(l), 0, l, col_text, col_bg);
            int finished = race_text(fb, col_text, col_bg, fin_frames > OFFER_FRAMES, hud);
            if (!finished && race_replaying() && hud) s2hud_band(fb, (int)draw_buf, band_text());       /* the replay's band while the player's car runs (0x4658c) */
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

        /* the statistics of the visit and of the last second (a frame the sequence's logos veil drew nothing: it is left out) */
        static struct stats scrap;
        struct stats *sa = veiled ? &scrap : &st_all;
        sa->sum += work;
        sa->n++;
        sa->faces += faces;
        sa->phys += phys_cycles;
        if (phys_cycles > sa->phys_max) sa->phys_max = phys_cycles;
        sa->others += others_cycles;
        if (others_cycles > sa->others_max) sa->others_max = others_cycles;
        sa->pieces += ts.pieces;
        sa->over += work > LATE_CYCLES;
        if (work > sa->max) {
            sa->max = work;
            sa->worst_s = s;
            if (*drive) {
                float node;
                memcpy(&node, &race_car(0)->rec[0x4b8 / 4], 4);       /* where the player is on the line (node, a float) */
                sa->worst_s = node > 0.0f ? node : 0.0f;
            }
            sa->w_pre = t_scene - t_frame;
            sa->w_cull = ts.cyc_cull;
            sa->w_scene = t_wait - t_scene - others_cycles - ts.cyc_cull;
            sa->w_others = others_cycles;
            sa->w_wait = work - (t_wait - t_frame);
            sa->w_faces = faces;
            sa->w_pieces = ts.pieces;
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
        if (attract_mode && att.over) {                 /* the sequence is over: its last frame (the S2 logo on black) is up; the log gets the statistics of the first one only (it has 4 KB) */
            static int logged;
            if (logged++) st_all.n = 0;
            leave(n, mode, &st_all);
            return 0;
        }
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
    race_custom = 0;                                    /* the arcade: the standard set-up, the computers' too (race.h); the championship and time attack set theirs after this */
    race_tuned = 0;
}

/* The player's set-up of a race of the championship or of time attack: the Custom the CAR page chose (cfg.setup, 1 .. 4) of a car of the first four, from the save; the bonus
   car's, Standard's and the arcade's is the standard one (docs/screamer2/driving.md "The set-up"). */
static void apply_setup(void)
{
    race_custom = 0;
    if (cfg.setup >= 1 && cfg.setup <= 4 && cfg.car >= 1 && cfg.car <= 4) {
        const int8_t *s = s2save_setup(cfg.team, cfg.car, cfg.setup - 1);
        if (s) {
            race_custom = 1;
            race_setup = (struct s2car_setup){ s[0], s[1], s[2], s[3], s[4], s[5] };
        }
    }
}

/* One race of the championship (s2league.h): the original's loading screen with the championship's records, the track, the race (visit in league mode) until the
   player has pressed CONTINUE on the end screen or chosen EXIT in the pause menu. 1 if the race was run, 0 if it could not be set up (the cars' places are then not
   there). */
static int league_race(struct zip *z, int n, int *cars, int *overlay)
{
    const char *what = "";
    const uint32_t *loading = s2load_show_kind(S2S_KIND_LEAGUE, n, race_laps);
    if (!loading) screen_text(n, "Loading...", "the track is built from the ZIP", 0, 0);
    if (!load_track(z, n, &what)) {
        char l[40];
        fmt_str(fmt_str(l, "Could not set up "), what);
        screen_text(n, l, "see debug.txt in the Saves folder", "B: back to the menu", 1);
        uint32_t keys_before = in_keys();
        for (int done = 0; !done;) {
            wait_swap_done();
            uint32_t keys = in_keys(), press = keys & ~keys_before;
            keys_before = keys;
            done = (press & KEY_B) != 0;
        }
        return 0;
    }
    int drive = 1, next;
    league_mode = 1;
    league_started = 0;
    while ((next = visit(n, cars, overlay, &drive)) < 0)
        ;                                               /* (the overlay or the gearbox changed: the same visit again) */
    if (!lg_res_valid) race_get_results(lg_res), lg_res_valid = 1;      /* a race left through the pause menu: its places as they stand */
    league_mode = 0;
    return league_started;
}

/* The championship (STARTL.EXE 0x4337d ..): LEAGUE SELECTION, WELCOME TO LEAGUE n, then the league's races on the tracks in the menu's order, each followed by the
   LEAGUE REPORT; after the last the report shows the win or the loss and the selection comes back. The player's team and car are the menu's, the laps and the
   gearbox the GAME SETTINGS', the computers' level the league's (s2league.h). Returns when the player leaves for the start menu. */
static void league_run(struct zip *z, int *cars, int *overlay)
{
    static int league_at;                               /* the cursor of LEAGUE SELECTION stays where it was (the original's 0x128a10) */
    for (;;) {
        if (s2lp_select(&league_at) != S2LP_GO) return;                 /* (ESC, or the picture is missing) */
        if (s2lp_welcome(league_at) == S2LP_BACK) continue;             /* (a missing picture leaves the page out) */
        struct s2_league L;
        s2l_begin(&L, league_at, cfg.team, cfg.car > 4 && !s2save_car5_open(cfg.team) ? 1 : cfg.car);
        dlog_str("league: ");
        dlog_dec(league_at + 1);
        dlog_str(" begins\n");
        for (;;) {
            const int n = s2l_track(L.races_done);
            const int tp = s2tp_league(league_at, L.races_done, n);         /* the original's TRACK page: the track's map, TERRAIN, DIFFICULTY (a missing picture leaves it out) */
            if (tp == S2TP_BACK || tp == S2TP_LEAVE) break;                 /* ESC before the first race, or YES to LEAVING LEAGUE?: back to LEAGUE SELECTION */
            const int cr = s2arcade_page(S2ARC_LEAGUE, &cfg);                /* the CAR page and its set-up: ESC is back to the TRACK page, a page that could not be shown is left out */
            if (cr != S2ARC_NEXT && cr != S2ARC_FAIL) continue;
            s2menu_store(&cfg);
            L.car = cfg.car > 4 && !s2save_car5_open(cfg.team) ? 1 : cfg.car;      /* (the grid is made of the car chosen for each race) */
            s2l_grid(&L, L.pick);
            apply_cfg();
            apply_setup();
            race_tuned = 1;                                                 /* the computers of the championship are tuned (0x14e20: the mode word without bit 1) */
            memcpy(race_grid, L.pick, sizeof race_grid);
            race_level = s2l_def[L.league].level;
            if (!league_race(z, n, cars, overlay)) return;
            struct race_result res[RACE_CARS];
            struct s2l_result lr;
            int place[S2L_CARS];
            memcpy(res, lg_res, sizeof res);            /* (as the end screen's ENTER took them, or as the race was left) */
            for (int i = 0; i < S2L_CARS; i++) place[i] = res[i].place;
            s2l_places(place, res[0].finished, &lr);
            s2l_add_points(&L, &lr);
            dlog_str("league: race ");
            dlog_dec(L.races_done);
            dlog_str(" T");
            dlog_dec(n);
            dlog_str(" first car ");
            dlog_dec(lr.car_at[0]);
            dlog_str(res[0].finished ? "" : " (player left)");
            dlog_char('\n');
            int r = s2lp_report(&L);
            if (r == S2LP_FAIL) r = s2l_over(&L) ? S2LP_DONE : S2LP_NEXT;       /* (no picture: go on) */
            if (r == S2LP_LEAVE) return;
            if (r == S2LP_DONE) {
                if (s2l_won(&L)) {                                      /* a league won: the desk scene (0x44144), after league 4 a second one with the fifth car, which is the chosen one from then on */
                    cfg.car = s2desk_won(league_at, cfg.team, cfg.car);
                    s2menu_store(&cfg);
                }
                break;
            }
        }
    }
}

/* One kind of time attack (cfg.free_run: the free run): the loading screen (time attack's records, none for the free run), the race against the ghost of the track's best run
   if GAME SETTINGS' TIME ATTACK GHOST CAR is ON and the track has one that can be raced (s2gh_ghost), or alone; the race is run again as often as the end screen asks (the ghost may
   be a new one then, which another car may drive: the image is built again only if it holds other cars). The player's grid place is the side the ghost did not start on (race.h). */
static void attack_race(struct zip *z, int *cars, int *overlay)
{
    const int free_run = cfg.free_run != 0, n = cfg.track;
    apply_cfg();
    apply_setup();
    race_kind = free_run ? RACE_FREE : RACE_ATTACK;
    race_laps = free_run ? 1 : 3;                                           /* (0x339e7: time attack three laps, the free run one that does not end) */
    attack_mode = free_run ? 2 : 1;
    for (;;) {
        const struct s2gh_head *g = !free_run && s2save_get(S2S_GHOST) == 0 ? s2gh_ghost(n, race_damage_key()) : 0;
        race_ghost = g;
        if (g) race_grid[1] = (struct s2_pick){ g->team, g->car };
        load_cars = g ? 2 : 1;
        if (!loaded.valid || loaded.track != n || loaded.cars != load_cars || memcmp(loaded.pick, race_grid, sizeof(struct s2_pick) * (size_t)load_cars)) {
            const char *what = "";
            const uint32_t *loading = s2load_show_kind(free_run ? S2S_KINDS : S2S_KIND_ATTACK, n, 3);
            if (!loading) screen_text(n, "Loading...", "the track is built from the ZIP", 0, 0);
            if (!load_track(z, n, &what)) {
                char l[40];
                fmt_str(fmt_str(l, "Could not set up "), what);
                screen_text(n, l, "see debug.txt in the Saves folder", "B: back to the menu", 1);
                uint32_t keys_before = in_keys();
                for (int done = 0; !done;) {
                    wait_swap_done();
                    uint32_t keys = in_keys(), press = keys & ~keys_before;
                    keys_before = keys;
                    done = (press & KEY_B) != 0;
                }
                break;
            }
        }
        dlog_str(free_run ? "attack: free run T" : "attack: T");
        dlog_dec(n);
        dlog_str(g ? " with the ghost\n" : " alone\n");
        int drive = 1, next;
        while ((next = visit(n, cars, overlay, &drive)) < 0)
            ;                                                               /* (the overlay or the gearbox changed: the same visit again) */
        if (next != VISIT_AGAIN) break;
    }
    attack_mode = 0;
    race_kind = RACE_NORMAL;
    race_ghost = 0;
    load_cars = RACE_CARS;
    s2menu_store(&cfg);
}

/* Time attack (STARTL.EXE 0x3f79e, 0x3cc2c): TRACK SELECTION with the turning maps -> the CAR page and its 3 LAPS / FREE RUN -> the race; ESC goes back a page (from TRACK
   SELECTION to the start menu). The best lap of the free run is forgotten when TRACK SELECTION is entered (0x3f7f3). Returns when the player leaves for the start menu. */
static void attack_run(struct zip *z, int *cars, int *overlay)
{
    for (;;) {
        race_free_reset();
        if (s2tp_attack(&cfg) != S2TP_GO) {                                 /* (ESC, or the picture or the maps are missing) */
            s2menu_store(&cfg);
            return;
        }
        s2menu_store(&cfg);
        if (s2arcade_page(S2ARC_ATTACK, &cfg) != S2ARC_NEXT) continue;      /* ESC: back to TRACK SELECTION */
        s2menu_store(&cfg);
        attack_race(z, cars, overlay);
        return;
    }
}

/* The attract sequence (s2attract.h; STARTL.EXE 0x3ca20): the menu was left alone for 30 s. The next track of the sequence's six is built (the screen stays as the page left it,
   black: the original shows nothing either while the race is set up), four computer cars, the default grid (team n, car n), at the level 0 (pro), no laps that count, the track's
   song cued for the third stage; visit() runs it as any race, with the cameras of the replay from the first frame and the sequence's screen in the race screen's place, until the screen's
   step says it is over (any button, or the last credit). Nothing of it is kept: no ghost or replay is recorded (race_attract), no best time or unlock, nothing is written to the save. */
static void attract_run(struct zip *z, int *cars, int at_start)
{
    if (!ui_ready || !s2hud_ready() || !s2att_load(&att, z)) {      /* (the sequence needs the race screen's sprites and fonts and its text) */
        dlog_str("attract: not shown (no sprites or text)\n");
        return;
    }
    const int n = s2att_track();
    if (!at_start) {                                    /* a cut to black (0x4085d: the fade is set to 1.0): the page that was up must not stay for the seconds the track takes (at the program's start the LOADING picture is up instead and visit() fades it out) */
        static const uint32_t black[256];
        struct blit_list *l = s2gfx_begin();
        blit_fill(l, 0, 0, FB_W, FB_H, 0);
        s2gfx_end(black, 0x8000);
    }
    apply_cfg();                                        /* (the settings that act in a race: MUSIC; the attract race is without DAMAGES: 0x19aaa returns at once when the mode word has 0x80) */
    s2car_damages = 0;
    race_tuned = 1;                                     /* the attract race's mode word (0x80) has no bit 1 either: the computers are tuned (0x14e20), car 0 too */
    for (int i = 0; i < RACE_CARS; i++) race_grid[i] = (struct s2_pick){ (uint8_t)(i + 1), (uint8_t)(i + 1) };    /* 0x3ca69: car i is team i, model i */
    race_level = 0;                                     /* 0x130d4c = 0 */
    race_laps = 99;                                     /* (no laps given: the original's record had 0 and no car ever finished; a car that does 99 laps is not a thing of 90 s) */
    race_attract = 1;
    attract_mode = 1;
    s2att_begin(&att);
    att_view = (struct s2att_view){ .black = 1, .sprite = -1, .caption = -1 };
    load_cars = RACE_CARS;
    const char *what = "";
    if (load_track(z, n, &what)) {
        int drive = 1, ov = 0;
        while (visit(n, cars, &ov, &drive) < 0)
            ;
    } else {
        dlog_str("attract: track not built: ");
        dlog_str(what);
        dlog_char('\n');
    }
    attract_mode = 0;
    race_attract = 0;
    s2snd_music_later = 0;
    s2att_next_track();                                 /* 0x3cbab: on to the next track, however the sequence ended */
    loaded.valid = 0;
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
    s2menu_init(z);
    s2menu_load(&cfg);
#if defined(S2_SIM_END) || defined(S2_SIM_INTRO)
    ui_ready = s2gfx_init(z);                           /* (the measurement builds of the end screen and of the intro have the toolkit in the simulation) */
#else
    ui_ready = !sim && s2gfx_init(z);                   /* (the simulation has no menus) */
#endif
#ifdef S2_SIM_INTRO
    int intro = ui_ready;                               /* (the measurement build of the intro: tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_INTRO=1 TAG=intro runs it in the simulation, on track 1's image) */
#else
    int intro = !sim && ui_ready;                       /* the program starts with the intro (the attract sequence), then the menu */
#endif
    if (intro) s2load_show_kind(S2S_KINDS, s2att_track(), 1);     /* the LOADING picture of the intro's track, up as soon as the toolkit can draw it: it stays through the rest of the start-up and the track's build, visit() fades it out */
    s2snd_init(z);
    s2hud_init(z);                                      /* the race screen (the simulation has it: its cost is measured there) */
    for (;;) {
        if (intro) {                                    /* the intro: any button ends it, the menu follows (on a first start TEAM SELECTION first) */
            intro = 0;
            attract_run(z, &cars, 1);
        }
        if (!sim) {                                     /* (the simulation takes the track from the testbench and has no menu) */
            int go = s2menu_run(&cfg);
            if (go == S2M_CHAMP) {                      /* the championship needs the toolkit's pages; without it the row does nothing */
                if (ui_ready) league_run(z, &cars, &overlay);
                continue;
            }
            if (go == S2M_ATTACK) {                     /* so does time attack */
                if (ui_ready) attack_run(z, &cars, &overlay);
                continue;
            }
            if (go == S2M_ATTRACT) {                    /* the menu was left alone for 30 s */
                attract_run(z, &cars, 0);
                continue;
            }
            n = cfg.track;
            drive = 1;
            apply_cfg();
            s2menu_log(&cfg);
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
