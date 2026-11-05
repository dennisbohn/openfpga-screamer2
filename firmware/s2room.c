/*
 * The options room (s2room.h)
 *
 * Read from STARTL.EXE with tools/dis.sh -f (flat addresses; the page is 0x38e00 .. 0x39568, its load list 0x113288: the menu font, the sprites, the texts, the team's
 * room, the screen), and checked against the running original frame by frame (tools/s2/dosbox_arcade.py --options polls the camera record and the page's variables
 * while it runs; docs/screamer2/menus.md). Every frame of the original: the keys, the animation clock, the camera, the engine's drawing, the band and the texts, the
 * arrows, the fade out. A frame is 1/50 s here (the original's own is the last frame's time, at least 1/50 s: the glides are per frame).
 *
 * The clocks: the engine's animation clock t (0x1347a0 is t x 65535) runs at 0.2 a second, from 0 where the page begins and back by 1 at 1; the page's hundredths (0x4cfd6)
 * count two a frame from 0 at the start of the page and go back to 0 where a station is left.
 *
 * The camera (0x131ea4: the angles a0 pitch, a1 heading, a2 0 in 2048ths, then the place x, y up, z): station k of P_<t>.MCM has a place P and a point A it looks at. A visit
 * starts the place at P + 4 (P - A), five times as far from A on the line (the first frame is the flight in), the angles at the look from there at A. A frame, not fading:
 *   - the place eased towards P (0x3364d: p = (p - P) f + P, cut towards zero, each of the three; f = 0.5 + 0.005 row, row 0x5f at first, 0x55 after the first move: the
 *     original turns the place about A by [0x1324a4] x 2 pi before it, a sway that only the CAR page of the set-ups' pages (not built) ever sets: 0 on every start)
 *   - the look from P at A (0x33905: heading = acos(dx / sqrt(dx^2 + dz^2)) (2 pi minus it for dz < 0) - pi / 2; pitch = acos(sqrt(dx^2 + dz^2) / |d|), 2 pi minus it
 *     for dy >= 0; d = P - A), the angles eased towards it the short way round (0x1016b, row 0x55 + 12 fade: pitch and heading in radians kept in 0 .. 2 pi), the record's
 *     a0 and a1 cut from them: x 1024 / pi, & 0x7ff
 * After ENTER (fade > 0, from the frame after it): the place eased towards A at row 0x62, the look from the place at A, the angles at row 0x55 + (cut) 12 fade; fade runs
 * 0.04 a frame, the picture's palette fades to black by it, and the page is over at 1.
 * LEFT / RIGHT (sound 5, MENU6) move to the next station at once: the station changes, the clock is 0, the row 0x55; ENTER (sound 7, MENU8) starts the fade, ESC
 * leaves at once for TEAM SELECTION; the keys are taken on a fresh press (0x389ae), the station is kept for the next visit (0x132378, zero when the menu starts).
 * The picture: the backdrop (0x83a68, MAP<t>.BCK) with the eye line at row 100 (the race's is at 65 % of the view, row 130), the room's placements, the band y 170 .. 200
 * through the darkening table of the palette (0x51868), the title (STRINGS.TXT "OPTIONS ROOM", 0x127eec) centred at y 4 in font 0, the choice's name (the five lines
 * after "MAIN MENU", 0x127ef4) at y 175 in font 1, colour 1, spacing 2, the arrows MENUL.SPR 1 at (10, 172) and 2 at (298, 172) where there is a station on that side,
 * while (clock & 0x7f) < 0x40.
 */
#include "s2room.h"
#include "s2scene.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "track.h"
#include "track_build.h"
#include "dat.h"
#include "frame.h"
#include "dlog.h"
#include "hw.h"
#include "files.h"
#include "trackmem.h"
#include <math.h>
#include <string.h>

#define SOUND_MOVE      5                       /* MENU.DTR ids (0x391c0, 0x39090) */
#define SOUND_ENTER     7
#define ROW_FIRST       0x5f                    /* the easing rows of the place */
#define ROW_MOVE        0x55
#define ROW_FADE        0x62
#define ROW_ANGLES      85                      /* + 12 fade */
#define FADE_FRAMES     25
#define FADE_STEP       0.04f                   /* 2.0 x the frame time of 0.02 s */
#define ANIM_STEP       0.004                   /* the animation clock: 0.2 a second x 0.02 s */
#define VIEW_EYE        100                     /* the projection's centre row of the menus' screen */
#define BACKDROP_H      154                     /* a view whose eye line (65 %) is at row 100: the backdrop is placed by it */
#define CULL_TOP        (-95)                   /* the culling's view: a clip 300 rows high with the eye line (65 %) at row 100, as track.c reads it */
#define CULL_BOTTOM     205
#define BAND_Y          170
#define BAND_H          30
#define SPR_ARROW_LEFT  1
#define SPR_ARROW_RIGHT 2

static int32_t mcm[2][S2ROOM_STATIONS][3];     /* [0] the places, [1] the points looked at */
static int station;                            /* kept from one visit to the next */
static int cached_team;                        /* the room in the track window (0: none) ... */
static uint8_t cached_head[0x100];             /* ... with the header its image has: a track built there since has another */

const int32_t *s2room_station(int k, int aim)
{
    return mcm[aim ? 1 : 0][k];
}

void s2room_forget(void)
{
    cached_team = 0;
}

/* the menu's room (s2menu_room): the memory of the viewer's track window, and the viewer told that its image is gone */
int s2room_menu(const struct s2_menu_cfg *c)
{
    static const struct s2room_mem mem = { GFX_MEM, GFX_MAX, (uint8_t *)(uintptr_t)TEX_BASE, TEX_MAX, SCRATCH_MEM, SCRATCH_MAX };
    loaded.valid = 0;                                   /* (what the window held is gone: time attack builds its track again) */
    return s2room_run(files_zip(), &mem, c->team);
}

/* ------------------------------------------------------------------------------------------------ the camera */

/* 0x33905: the angles of the look from pos at aim (d = pos - aim), the floats it stores at 0x1324a8 (pitch) and 0x1324ac (heading) */
static void look_at(const int32_t pos[3], const int32_t aim[3], float *pitch, float *head)
{
    const int32_t dx = pos[0] - aim[0], dy = pos[1] - aim[1], dz = pos[2] - aim[2];
    const double horiz2 = (double)((int64_t)dx * dx + (int64_t)dz * dz);
    /* 0x10d9e: the angle round the vertical, acos of dx over the length, 2 pi minus it for dz < 0 (the constants are the floats of the program) */
    float v = (float)acos((double)dx / sqrt(horiz2));
    if (dz < 0) v = (float)((double)6.2831855f - (double)v);
    *head = (float)((double)v + (double)-1.5707964f);
    const float horiz = (float)sqrt(horiz2), total = (float)sqrt((double)((int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz));
    const float ratio = (float)((double)horiz / (double)total);
    const double a = fabs(acos((double)ratio));
    *pitch = dy < 0 ? (float)a : (float)(6.283185482025146 - a);
}

/* the place eased towards a target (0x3364d): in the x87's own precision, the sum cut towards zero */
static void ease_place(int32_t pos[3], const int32_t target[3], int row)
{
    const double f = (double)s2scene_easing(row);
    for (int k = 0; k < 3; k++) pos[k] = (int32_t)(((double)pos[k] - (double)target[k]) * f + (double)target[k]);
}

/* the record's angle from the radians: x 1024 / pi, cut, & 0x7ff */
static int angle_of(float rad)
{
    return (int)((double)rad * 1024.0 / 3.1415927410125732) & 0x7FF;
}

void s2room_cam_start(struct s2room_cam *c, int k)
{
    const int32_t *P = mcm[0][k], *A = mcm[1][k];
    for (int i = 0; i < 3; i++) c->pos[i] = P[i] + 4 * (P[i] - A[i]);
    look_at(c->pos, A, &c->pitch, &c->head);
    c->a0 = angle_of(c->pitch);
    c->a1 = angle_of(c->head);
    c->row = ROW_FIRST;
}

void s2room_cam_step(struct s2room_cam *c, int k, float fade)
{
    const int32_t *P = mcm[0][k], *A = mcm[1][k];
    float lp, lh;
    if (fade == 0.0f) {
        ease_place(c->pos, P, c->row);
        look_at(P, A, &lp, &lh);
    } else {
        ease_place(c->pos, A, ROW_FADE);
        look_at(c->pos, A, &lp, &lh);
    }
    const int row = (int)((double)fade * 12.0 + 85.0);
    s2scene_ease_angle(&c->pitch, lp, row);
    s2scene_ease_angle(&c->head, lh, row);
    c->a0 = angle_of(c->pitch);
    c->a1 = angle_of(c->head);
}

/* the camera as the track's engine takes it: place (x, y = the engine's z, height = its y), heading and pitch (+ up) from the record's angles (a0 1024 .. 2047 is down) */
static struct track_cam track_cam_of(const struct s2room_cam *c)
{
    const int pitch = c->a0 >= 1024 ? c->a0 - 2048 : c->a0;
    return (struct track_cam){ (float)c->pos[0], (float)c->pos[2], (float)c->pos[1], s2scene_rad(c->a1), s2scene_rad(pitch), 0.0f };
}

/* ------------------------------------------------------------------------------------------------ loading */

static int rd32s(const uint8_t *p)
{
    return (int)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

int s2room_stations(const uint8_t *file, uint32_t size)
{
    if (size < 4) return 0;
    const int n = rd32s(file);
    if (n < S2ROOM_STATIONS || n > 15 || size < 4 + 24 * (uint32_t)n) return 0;     /* (a record of the game's table holds at most 15 stations: 0x16c bytes) */
    for (int a = 0; a < 2; a++)
        for (int k = 0; k < S2ROOM_STATIONS; k++)
            for (int i = 0; i < 3; i++) mcm[a][k][i] = rd32s(file + 4 + 4 * (3 * (a * n + k) + i));
    return 1;
}

int s2room_load(struct zip *z, const struct s2room_mem *m, int team)
{
    if (team < 1 || team > 4) return 0;
    if (cached_team == team && !memcmp(cached_head, m->image, sizeof cached_head)) return 1;     /* (a track built in the window since has another header: a car section) */
    cached_team = 0;
    const uint32_t t0 = rdcycle();
    char name[24] = "GFX/MENU11.DAT";
    name[8] = (char)('0' + team);
    const struct zip_entry *e = zip_find(z, name);
    if (!e || e->size + ZIP_SLACK > m->container_max || zip_read(z, e, m->container, m->container_max)) {
        dlog_str("room: ");
        dlog_str(name);
        dlog_str(" could not be read\n");
        return 0;
    }
    const struct s2_files f = { .gfx = m->container, .gfx_size = e->size };     /* no cars, no track data: the scenery (track_build.h) */
    struct track_build_info info;
    const uint32_t size = track_build_s2(&f, team, m->image, m->image_max, m->scratch, m->scratch_max, &info);
    while (!(MEM_STATUS & MEM_WRITES_IDLE))                     /* all of it in the SDRAM for the 3D units */
        ;
    dcache_invalidate();                                        /* (the CPU reads the image back) */
    if (!size || !track_open(m->image) || !track_init()) {
        dlog_str("room: image not built, stopped at ");
        dlog_str(size ? "its header" : info.fail);
        dlog_char('\n');
        return 0;
    }
    char mcm_name[16] = "P_1.MCM";
    mcm_name[2] = (char)('0' + team);
    struct dat_entry d;
    static uint32_t buf[64];                                    /* the file: 252 bytes */
    if (!dat_find(m->container, e->size, mcm_name, &d) || d.size > sizeof buf || dat_extract(m->container, e->size, &d, (uint8_t *)buf, sizeof buf) != d.size ||
        !s2room_stations((const uint8_t *)buf, d.size)) {
        dlog_str("room: P_t.MCM is not usable\n");
        return 0;
    }
    cached_team = team;
    memcpy(cached_head, m->image, sizeof cached_head);
    dlog_str("room: T");
    dlog_dec(team);
    dlog_char(' ');
    dlog_dec((int32_t)size);
    dlog_str(" bytes in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the page */

static struct room {
    struct s2room_cam cam;
    int clock;                                  /* the page's hundredths */
    float anim;                                 /* the engine's animation clock */
    float fade;                                 /* the fade out's level, 0 until the frame after ENTER */
    int leaving, fresh;
    uint32_t before;                            /* the buttons held a frame ago */
    const char *title, *name[S2ROOM_STATIONS];
    uint8_t dark[256];
} R;

const struct s2room_cam *s2room_camera(int *at)
{
    if (at) *at = station;
    return &R.cam;
}

static void room_frame(void *self, uint32_t tick)
{
    struct room *p = self;
    p->clock = tick ? p->clock + 2 : 0;
    double t = (double)p->anim + (double)0.02f * 0.2;           /* 0x39000: t += dt x 0.2 (a float and a double), back by 1 at 1 */
    p->anim = (float)t;
    if (p->anim >= 1.0f) p->anim = (float)((double)p->anim + -1.0);
    track_clock_set(p->anim);
}

static int room_key(void *self, uint32_t press)
{
    struct room *p = self;
    (void)press;                                        /* (the original takes a press once: no repeat) */
    const uint32_t keys = in_keys(), edge = keys & ~p->before;
    p->before = keys;
    if (p->leaving) return 0;
    if (edge & S2PAGE_ESC) return S2ROOM_BACK;
    if (edge & KEY_X) return S2ROOM_DEV;
    if (edge & S2PAGE_ENTER) {
        s2sound_menu(SOUND_ENTER);
        p->leaving = 1;
        return station + 1;
    }
    const int d = (edge & KEY_RIGHT) ? 1 : (edge & KEY_LEFT) ? -1 : 0;
    if (d && station + d >= 0 && station + d < S2ROOM_STATIONS) {
        station += d;
        p->clock = 0;
        p->cam.row = ROW_MOVE;
        s2sound_menu(SOUND_MOVE);
    }
    return 0;
}

/* the picture without the texts: the backdrop and the ground colour under it, the room, the band darkened */
static void room_scene(struct room *p, struct blit_list *l, const struct track_cam *cam)
{
    const int buffer = l->buffer;
    const struct rs_clip backdrop = { 0, 0, FB_W, BACKDROP_H }, cull = { 0, CULL_TOP, FB_W, CULL_BOTTOM };
    struct track_cam sky = *cam;
    sky.yaw += 1.5707964f;                                      /* (the room's backdrop turns a quarter of a turn further than the race's: the column is (1023 - a1) / 2, measured) */
    track_backdrop_blit(l, &backdrop, &sky);
    blit_fill(l, 0, BACKDROP_H, FB_W, FB_H - BACKDROP_H, track_ground_colour());
    s2scene_begin(track_mlb(), buffer, p->fresh);
    p->fresh = 0;
    track_mode = TRACK_HW;
    track_hw_piece = hw_piece;
    struct track_stats ts;
    track_draw_cam(0, &cull, cam, 0, 0, &ts);
    s2scene_end();
    while (RS_DRAW & RS_BUSY)                                   /* (the band is darkened by the CPU: the units are done first) */
        ;
    s2gfx_dark_fb(FB(buffer), 0, BAND_Y, FB_W, BAND_H, p->dark);
    blit_start(l, buffer);
}

/* the title, the choice's name and the arrows over it */
static void room_texts(const struct room *p, struct blit_list *l, int at, int clock)
{
    s2gfx_text_centred(l, S2G_FONT_TITLE, S2G_WHITE, 2, 4, p->title);
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 175, p->name[at]);
    if ((clock & 0x7F) < 0x40) {
        if (at > 0) s2gfx_sprite(l, SPR_ARROW_LEFT, 10, 172);
        if (at < S2ROOM_STATIONS - 1) s2gfx_sprite(l, SPR_ARROW_RIGHT, 298, 172);
    }
}

static void room_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct room *p = self;
    (void)tick;
    s2room_cam_step(&p->cam, station, p->fade);
    const struct track_cam cam = track_cam_of(&p->cam);
    room_scene(p, l, &cam);
    room_texts(p, l, station, p->clock);
    if (p->leaving) p->fade = (float)((double)0.02f * 2.0 + (double)p->fade);
}

/* a black screen while the room is read (the picture before it, the name page's flash, would stay up for two seconds) */
static void blank(void)
{
    static const uint32_t black[S2G_PALETTE];
    struct blit_list *l = s2gfx_begin();
    blit_fill(l, 0, 0, FB_W, FB_H, 0);
    s2gfx_end(black, 0x8000);
}

int s2room_run(struct zip *z, const struct s2room_mem *m, int team)
{
    if (!s2gfx_ready()) return S2ROOM_FAIL;
    if (cached_team != team) blank();                           /* (the room is read and built: about two seconds) */
    if (!s2room_load(z, m, team)) return S2ROOM_FAIL;
    struct room *p = &R;
    memset(p, 0, sizeof *p);
    p->title = s2gfx_line("OPTIONS ROOM", 0, "OPTIONS ROOM");
    static const char *const names[S2ROOM_STATIONS] = { "ARCADE", "CHAMPIONSHIP", "TIME ATTACK", "MULTIPLAYER", "GAME SETTINGS" };
    for (int k = 0; k < S2ROOM_STATIONS; k++) p->name[k] = s2gfx_line("MAIN MENU", 1 + k, names[k]);
    s2room_cam_start(&p->cam, station);
    s2gfx_dark_table(track_palette(), p->dark, 256);
    p->fresh = 1;
    p->before = in_keys();
    const struct s2_page page = { 0, track_palette(), 0, FADE_FRAMES, room_frame, room_key, room_draw };
    return s2page_run(&page, p);
}
