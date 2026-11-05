/*
 * The track pages (s2trackpage.h)
 *
 * Read from STARTL.EXE with tools/dis.sh -f (flat addresses): time attack's TRACK SELECTION is 0x3f79e .. 0x3ffaa, the league's TRACK page 0x3ee28 .. 0x3f79d. A frame
 * of either: the keys, the clock (0x4cfd6: hundredths, two a frame, set to 0 where the page begins and where time attack's choice moves), every map turned by
 * 256 x the frame's seconds (5.12 a frame, cut: 5 of 2048) about the vertical, the picture, the engine's drawing of the objects in the list (0x75570), the texts.
 *
 * The camera (the same in both): at (1104, 3300, -1428) looking at (1116, 75, 588), which the routine 0x33905 turns into angles of 2048ths: a0 = 694 and a1 = 1022 (the
 * pitch counted from the other side: 1024 - 694 = 330 down, 58 degrees; a half turn less two about the vertical, looking along +z). The maps stand at the
 * origin (their blocks each moved by their position record), a1 = 0 at the start.
 *
 * The time attack choice: each of the seven maps is an object of the engine, of which only the chosen one and the one it replaced are in the list. LEFT / RIGHT (only when the
 * last move is over: t = 1) make the chosen one the old one and the neighbour the new one and start t at 0, to 1 at 4 a second (0.08 a frame); while t < 1 the old
 * map moves from the origin to a point and the new one to the origin from another (0x33873: out = b + (a - b) t, cut):
 *    LEFT:  the old one to B = (-12800, -51200, -12800), the new one from A = (-15360, -2560, 0)
 *    RIGHT: the old one to A, the new one from B
 * (B is far below and behind the camera: out of sight; A is far to the left). The banner and the texts show the old track until t is 1 (0x3fd1c). A move plays
 * sound 12 (MENU13), ENTER sound 13 (MENU14) and fades out over 13 frames (0.08 a frame); ESC leaves at once.
 *
 * The picture's right half: the box x 168 .. 314, y 94 .. 180 darkened (0x51868, over the map), the banner sprite 0x13 + track - 1 centred in the right half at y 44, TERRAIN: at
 * (173, 96) and its two lines at (185, 108) and (185, 120), DIFFICULTY: at (173, 144) and its line at (185, 156) (font 2, colour 1, spacing 2; STRINGS.TXT: the lines
 * after "TERRAIN:" and "DIFFICULTY:": three a track in the files' order), the title at y 4 (font 0), the arrows (MENUL.SPR 1 and 2 at (10, 172) and (298, 172)) where
 * there are tracks to the left and right, while (clock & 0x7f) < 0x40.
 */
#include "s2trackpage.h"
#include "s2trackmap.h"
#include "s2arcade.h"
#include "s2scene.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "s2save.h"
#include "dlog.h"
#include "hw.h"
#include <string.h>

#define FADE_OUT    13
#define SOUND_MOVE  12
#define SOUND_ENTER 13
#define SMALL       S2G_FONT_SMALL
#define SMALL_HOT   S2G_FONT_SMALL2
#define WHITE       1
#define HOT         3
#define A0          694                         /* the camera's angles (see above) */
#define A1          1022
#define LEAVE_NOW   (-2)

enum { SPR_ARROW_LEFT = 1, SPR_ARROW_RIGHT = 2, SPR_BANNER = 0x13 };

static uint32_t pal[S2G_PALETTE];
static uint8_t dark[256];
static uint32_t map_generation;

static int picture(const char *name)
{
    if (!s2gfx_ready() || !s2gfx_bkk(name, s2gfx_pic(0), pal)) {
        dlog_str("ui: ");
        dlog_str(name);
        dlog_str(" not usable\n");
        return 0;
    }
    s2gfx_dark_table(pal, dark, 256);
    return 1;
}

/* the maps are in the models area, which the other 3D pages use too: read again when one of them has been there */
static int maps_ready(void)
{
    if (s2map_get(1) && s2scene_generation() == map_generation) return 1;
    if (!s2map_load(s2gfx_zip())) return 0;
    map_generation = s2scene_generation();
    return 1;
}

struct track_cam s2tp_camera(void)
{
    return (struct track_cam){ 1104.0f, -1428.0f, 3300.0f, s2scene_rad(A1), -s2scene_rad(1024 - A0), 0.0f };
}

/* ------------------------------------------------------------------------------------------------ what both pages draw */

struct item {
    int track;
    int pos[3];
};

/* the picture is in the list; the maps through the geometry unit (far pieces first), the box of the text darkened over them, the list empty again */
static void draw_maps(struct blit_list *l, const struct item *it, int n, int a1, int *fresh)
{
    const int buffer = l->buffer;
    const struct track_cam cam = s2tp_camera();
    struct track_block b[2 * S2MAP_BLOCKS];
    float key[2 * S2MAP_BLOCKS];
    int m = 0;
    for (int i = 0; i < n; i++) {
        struct track_block bb[S2MAP_BLOCKS];
        float kk[S2MAP_BLOCKS];
        const int got = s2map_blocks(it[i].track, &cam, (float)it[i].pos[0], (float)it[i].pos[1], (float)it[i].pos[2], a1, bb, kk);
        for (int k = 0; k < got; k++) {
            int j = m++;
            for (; j > 0 && key[j - 1] < kk[k]; j--) {          /* all pieces of the objects far to near */
                b[j] = b[j - 1];
                key[j] = key[j - 1];
            }
            b[j] = bb[k];
            key[j] = kk[k];
        }
    }
    s2scene_begin(s2map_mlb(), buffer, *fresh);
    *fresh = 0;
    s2scene_blocks(&cam, b, m);
    s2scene_end();
    while (RS_DRAW & RS_BUSY)                                   /* (the picture is darkened by the CPU: the units are done first) */
        ;
    s2gfx_dark_fb(FB(buffer), 168, 94, 146, 86, dark);
    blit_start(l, buffer);
}

/* the banner, the texts of the track (file number 1 .. 7) */
/* the CAR page's second state in time attack (0x3de0b ..): 3 LAPS and FREE RUN in the two halves of the screen at y 155, the chosen one lit in the phase `lit_phase` (bit 0x20 of the
   clock clear); drawn by s2arcade.c's car page, here so that the test against the running original reaches it without the page's cars */
void s2tp_modes_draw(struct blit_list *l, int free_run, int lit_phase)
{
    for (int k = 0; k < 2; k++) {
        const int lit = lit_phase && free_run == k;
        s2gfx_text_boxed(l, lit ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, lit ? 3 : S2G_WHITE, 2, 160 * k, 160, 155, s2gfx_line("3 LAPS", k, k ? "FREE RUN" : "3 LAPS"));
    }
}

static void draw_track_texts(struct blit_list *l, int track)
{
    s2gfx_sprite_boxed(l, SPR_BANNER + track - 1, 160, 160, 44);
    const int t3 = 3 * (track - 1);
    s2gfx_text(l, SMALL, WHITE, 2, 173, 96, s2gfx_line("TERRAIN:", 0, "TERRAIN:"));
    /* (the Tunnel has an empty second line of terrain in the file, which the toolkit's list leaves out like every empty line: its difficulty is the line after the first) */
    const int tunnel = track == 7;
    s2gfx_text(l, SMALL, WHITE, 2, 185, 108, s2gfx_line("TERRAIN:", 2 + t3, ""));
    if (!tunnel) s2gfx_text(l, SMALL, WHITE, 2, 185, 120, s2gfx_line("TERRAIN:", 3 + t3, ""));
    s2gfx_text(l, SMALL, WHITE, 2, 173, 144, s2gfx_line("TERRAIN:", 1, "DIFFICULTY:"));
    s2gfx_text(l, SMALL, WHITE, 2, 185, 156, s2gfx_line("TERRAIN:", tunnel ? 3 + t3 : 4 + t3, ""));
}

/* ------------------------------------------------------------------------------------------------ time attack's TRACK SELECTION */

struct ta {
    struct s2_menu_cfg *cfg;
    int cur, prev, n, dir;                                      /* the chosen place of the original's order, the one left, how many are open, the last move (-1, +1) */
    float t;                                                    /* the move: 0 .. 1 */
    int a1;
    uint32_t now, t0;
    int fresh;
};

/* the points the maps fly between (0x3f9f8, 0x3fa17, 0x3fa78 ..) */
static const int A[3] = { -15360, -2560, 0 };
static const int B[3] = { -12800, -51200, -12800 };

/* 0x33873: b + (a - b) t for each of the three, cut */
static void lerp(int out[3], const int a[3], const int b[3], float t)
{
    for (int i = 0; i < 3; i++) out[i] = (int)((double)b[i] + (double)(a[i] - b[i]) * (double)t);
}

static void ta_frame(void *self, uint32_t tick)
{
    struct ta *p = self;
    p->now = tick;
    p->a1 = (p->a1 + 5) & 0x7FF;
}

static int ta_key(void *self, uint32_t press)
{
    struct ta *p = self;
    if (press & S2PAGE_ESC) return S2TP_BACK;
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_ENTER);
        return S2TP_GO;
    }
    const int left = (press & KEY_LEFT) != 0, right = (press & KEY_RIGHT) != 0;
    if (((left && p->cur > 0) || (right && p->cur < p->n - 1)) && p->t == 1.0f) {
        p->prev = p->cur;
        p->cur += right ? 1 : -1;
        p->dir = right ? 1 : -1;
        p->t = 0.0f;
        p->t0 = p->now;
        p->cfg->track = s2save_track_at(p->cur);
        s2sound_menu(SOUND_MOVE);
    }
    return 0;
}

static void ta_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct ta *p = self;
    (void)tick;
    struct item it[2];
    int n = 0;
    it[n++] = (struct item){ s2save_track_at(p->cur), { 0, 0, 0 } };
    if (p->prev != p->cur) {                                    /* the old map is in the list for good; after the move it stands out of sight */
        struct item *o = &it[n++];
        o->track = s2save_track_at(p->prev);
        const int zero[3] = { 0, 0, 0 };
        lerp(o->pos, p->dir < 0 ? B : A, zero, p->t);           /* LEFT: the old one to B (and the new from A), RIGHT: the old one to A (the new from B) */
        lerp(it[0].pos, zero, p->dir < 0 ? A : B, p->t);
    }
    draw_maps(l, it, n, p->a1, &p->fresh);
    const int clock = (int)((p->now - p->t0) * 2);
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("TRACK SELECTION", 0, "TRACK SELECTION"));
    draw_track_texts(l, s2save_track_at(p->t == 1.0f ? p->cur : p->prev));
    if ((clock & 0x7F) < 0x40) {
        if (p->cur > 0) s2gfx_sprite(l, SPR_ARROW_LEFT, 10, 172);
        if (p->cur < p->n - 1) s2gfx_sprite(l, SPR_ARROW_RIGHT, 298, 172);
    }
    p->t += 0.08f;                                              /* (4 a second, 0x133350 x 4: 12.5 frames) */
    if (p->t > 1.0f) p->t = 1.0f;
}

int s2tp_attack(struct s2_menu_cfg *c)
{
    if (!maps_ready() || !picture("LEAG_0L.BKK")) return S2TP_FAIL;
    struct ta p = { .cfg = c, .n = s2arcade_tracks_open(), .t = 1.0f, .fresh = 1 };
    for (int i = 0; i < p.n; i++)
        if (s2save_track_at(i) == c->track) p.cur = i;
    p.prev = p.cur;
    c->track = s2save_track_at(p.cur);                          /* (a track that is not open: the first) */
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, ta_frame, ta_key, ta_draw };
    return s2page_run(&page, &p);
}

/* ------------------------------------------------------------------------------------------------ the league's TRACK page */

struct lg {
    int league, race, track;
    int asking, yes;                                            /* LEAVING LEAGUE? asked, YES chosen (NO is chosen first) */
    int a1, fresh;
    uint32_t now;
};

static void lg_frame(void *self, uint32_t tick)
{
    struct lg *p = self;
    p->now = tick;
    p->a1 = (p->a1 + 5) & 0x7FF;
}

static int lg_key(void *self, uint32_t press)
{
    struct lg *p = self;
    if (p->asking) {
        if (press & KEY_LEFT) p->yes = 1;
        else if (press & KEY_RIGHT) p->yes = 0;
        if (press & S2PAGE_ESC) p->asking = 0;
        else if (press & S2PAGE_ENTER) {
            if (p->yes) return LEAVE_NOW;
            p->asking = 0;
        }
        return 0;
    }
    if (press & S2PAGE_ESC) {
        if (p->race == 0) return S2TP_BACK;                     /* before the first race: back to LEAGUE SELECTION */
        p->asking = 1;
        p->yes = 0;
        return 0;
    }
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_ENTER);
        return S2TP_GO;
    }
    return 0;
}

static void lg_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct lg *p = self;
    (void)tick;
    const struct item it = { p->track, { 0, 0, 0 } };
    draw_maps(l, &it, 1, p->a1, &p->fresh);
    const int clock = (int)(p->now * 2);
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("LEAGUE 1", 2 * p->league, "LEAGUE"));
    draw_track_texts(l, p->track);
    if (p->asking) {                                            /* the page darkened, the question in its left half */
        const int buffer = l->buffer;
        s2gfx_flush();
        while (RS_DRAW & RS_BUSY)
            ;
        s2gfx_dark_fb(FB(buffer), 0, 0, 320, 200, dark);
        blit_start(l, buffer);
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 0, 160, 80, s2gfx_line("LEAVING LEAGUE", 0, "LEAVING LEAGUE"));
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 0, 160, 95, s2gfx_line("LEAVING LEAGUE", 1, "ARE YOU SURE ?"));
        const int blink = (clock & 0x3F) <= 0x20;
        const int yes_on = p->yes && blink, no_on = !p->yes && blink;
        s2gfx_text_boxed(l, yes_on ? SMALL_HOT : SMALL, yes_on ? HOT : WHITE, 2, 0, 80, 115, s2gfx_line("LEAVING LEAGUE", 2, "YES"));
        s2gfx_text_boxed(l, no_on ? SMALL_HOT : SMALL, no_on ? HOT : WHITE, 2, 80, 80, 115, s2gfx_line("LEAVING LEAGUE", 3, "NO"));
    } else if ((clock & 0x7F) < 0x40) {
        s2gfx_text_centred(l, SMALL, WHITE, 2, 184, s2gfx_line("ACCELERATE TO CONTINUE", 0, "ACCELERATE TO CONTINUE"));
    }
}

int s2tp_league(int league, int race, int track)
{
    char name[16] = "TKG_1L.BKK";
    if (track < 1 || track > 6) return S2TP_FAIL;               /* (the league never races on the Tunnel: no TKG_7L) */
    name[4] = (char)('0' + track);
    if (!maps_ready() || !picture(name)) return S2TP_FAIL;
    struct lg p = { .league = league, .race = race, .track = track, .fresh = 1 };
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, lg_frame, lg_key, lg_draw };
    int r = s2page_run(&page, &p);
    return r == LEAVE_NOW ? S2TP_LEAVE : r;
}
