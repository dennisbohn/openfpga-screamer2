/*
 * The arcade pages (s2arcade.h)
 *
 * What the original's pages do, read from STARTL.EXE (tools/dis.sh -f; flat addresses): the page runs `while (!done)`: the picture, the
 * page's sprites and text, the fade, the frame. A page is cut in (no fade in); ENTER (the keys' code 0x10) fades the screen to black over
 * 25 frames (0.04 a frame: 2.0 x the frame time 0.02; the keys are ignored meanwhile) and ESC (0x20) leaves at once. The blinking is
 * the timer's hundredths (0x4cfd6, 100 a second, two a frame at 50 pictures a second), here tick * 2 from the start of the page (the
 * original's clock runs on from the program's start; it is set back to 0 where a choice changes, as here).
 *
 *   ARCADE title (0x3b6e8): ARCADE_L.BKK, "ACCELERATE TO CONTINUE" (STRINGS line 15) centred at y 184 in font 2, colour 1, spacing 2,
 *                           shown while (clock & 0x7f) < 0x40. ENTER: sound 1, fade, on to the TRACK page. ESC: the options room.
 *                           (The original also drives four cars of the team's over the picture: a 3D scene, not built.)
 *   TRACK (0x41a69):        BKG_1L.BKK, the sprite 105 "TRACK selection" at (28, 15); every open track is a sprite 92 x 116 at y 64 and
 *                           x = 105 i + scroll: 5 + track - 1 for the chosen one, 7 more for the others (they are pictures with the track's
 *                           name banner, in MENUL.SPR); the scroll eases to 8 - 105 k (k the first of the three shown: the chosen one, or
 *                           the one before it, or two before at the end) by 0.75 of the distance a frame (0x111d6, row 50 of the easing
 *                           tables); sprite 0, the frame, at y 60 and x 105 w + 2 (w the chosen one's place of the three) eased the same
 *                           way, shown while (clock & 0x3f) < 0x20; the arrows (sprites 1 and 2 at (10, 170) and (298, 170)) when there
 *                           are tracks to the left (k > 0) or the right (k + 2 < last), while (clock & 0x7f) < 0x40. LEFT / RIGHT move
 *                           (not round), sound 0, the clock set back; ENTER: sound 2, fade, on to the CAR page; ESC: back to the options
 *                           room, not to the title. The tracks come in the original's order (England, Egypt, California, Finland,
 *                           Switzerland, Columbia, The Tunnel), the open ones as the save has them (s2save_track_at).
 *   CAR (0x40c31, state 0): BKG_2L.BKK, sprite 34 "CAR selection" at (28, 15), the team's badge (35 + team) at (226, 1), the car's plate at
 *                           (18, 155): sprite 26 + car for the four cars, 30 + team for the fifth. The original shows the team's cars
 *                           in 3D on a carousel (LEFT / RIGHT turn it, sound 4; the team was chosen on an earlier page): here the
 *                           team's name and a hint stand in for the 3D, and UP / DOWN choose the team. ENTER: on to the TRANSMISSION
 *                           (no sound, no fade, the same page); ESC: back to the TRACK page.
 *   TRANSMISSION (state 1): the same picture, sprite 39 "select TRANSMISSION" at (28, 15), the badge, AUTOMATIC at (18, 155) and MANUAL at
 *                           (159, 155): sprites 40 / 42 dim, 41 / 43 lit; the chosen one is lit while (clock & 0x20) == 0, the other is
 *                           always dim. LEFT / RIGHT choose (sound 0, the clock set back); ENTER: sound 3, fade, the race; ESC: back to
 *                           the CAR page (no sound).
 */
#include "s2arcade.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "s2save.h"

#define FADE_FRAMES     25
#define SOUND_CURSOR    0                   /* MENU.DTR ids (MENU1 .. MENU15 are 0 .. 14) */
#define SOUND_TITLE     1
#define SOUND_TRACK     2
#define SOUND_TRANS     3
#define SOUND_CAR       4

enum {
    SPR_FRAME = 0, SPR_ARROW_LEFT = 1, SPR_ARROW_RIGHT = 2, SPR_THUMB = 5, SPR_THUMB_DIM = 12,
    SPR_PLATE = 26, SPR_PLATE_BONUS = 30, SPR_CAR_TITLE = 34, SPR_BADGE = 35, SPR_TRANS_TITLE = 39, SPR_AUTO = 40, SPR_MANUAL = 42,
    SPR_TRACK_TITLE = 105
};

#define THUMB_PITCH     105                 /* a thumbnail and the gap to the next */
#define THUMB_Y         64
#define FRAME_Y         60

static uint32_t pal[S2G_PALETTE];

/* v + d round the places first .. first + count - 1 */
static int ring(int v, int d, int first, int count)
{
    return (v - first + d + 4 * count) % count + first;
}

static int load_picture(const char *name)
{
    return s2gfx_bkk(name, s2gfx_pic(0), pal);
}

/* ------------------------------------------------------------------------------------------------ the title */

static int title_key(void *self, uint32_t press)
{
    (void)self;
    if (press & S2PAGE_ESC) return S2ARC_BACK;
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_TITLE);
        return S2ARC_NEXT;
    }
    return 0;
}

static void title_draw(void *self, struct blit_list *l, uint32_t tick)
{
    (void)self;
    if (((tick * 2) & 0x7F) < 0x40)
        s2gfx_text_centred(l, S2G_FONT_SMALL, S2G_WHITE, 2, 184, s2gfx_line("ACCELERATE TO CONTINUE", 0, "ACCELERATE TO CONTINUE"));
}

/* ------------------------------------------------------------------------------------------------ the track page */

struct track_page {
    struct s2_menu_cfg *cfg;
    int pos, n;                             /* the chosen place in the original's order, how many places are open */
    float scroll, frame;                    /* the thumbnails' and the frame's x, eased; 0: not set yet (the first frame takes the target) */
    uint32_t now, t0;                       /* the frame, and the frame the blinking began at */
};

int s2arcade_tracks_open(void)
{
    int n = 0;
    while (n < S2S_TRACKS && s2save_track_open(s2save_track_at(n))) n++;
    return n < 3 ? 3 : n;
}

static void track_frame(void *self, uint32_t tick)
{
    ((struct track_page *)self)->now = tick;
}

static void track_move(struct track_page *p, int pos)
{
    p->pos = pos;
    p->cfg->track = s2save_track_at(pos);
    p->t0 = p->now;
    s2sound_menu(SOUND_CURSOR);
}

static int track_key(void *self, uint32_t press)
{
    struct track_page *p = self;
    if (press & S2PAGE_ESC) return S2ARC_BACK;
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_TRACK);
        return S2ARC_NEXT;
    }
    if ((press & KEY_LEFT) && p->pos > 0) track_move(p, p->pos - 1);
    else if ((press & KEY_RIGHT) && p->pos < p->n - 1) track_move(p, p->pos + 1);
    else if (press & (KEY_L | KEY_R))       /* (not the original's: three places on, round, so that every track is two presses away) */
        track_move(p, ring(p->pos, (press & KEY_R) ? 3 : -3, 0, p->n));
    return 0;
}

/* value = (value - target) * 0.75 + target (0x111d6 with row 50), then the page's test: the target itself when within half a place (the
   thumbnails: half a place or less; the frame: less than half) */
static float ease(float value, float target, int or_equal)
{
    value = (value - target) * 0.75f + target;
    float d = target - value;
    d = d < 0 ? -d : d;
    return (or_equal ? d <= 0.5f : d < 0.5f) ? target : value;
}

static void track_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct track_page *p = self;
    int last = p->n - 1, pos = p->pos;
    int first = pos == 0 ? 0 : last - pos >= 1 ? pos - 1 : pos - 2;        /* the first of the three shown */
    int slot = pos == 0 ? 0 : pos == last ? 2 : 1;                          /* the chosen one's place among them */
    uint32_t clock = (tick - p->t0) * 2;
    s2gfx_sprite(l, SPR_TRACK_TITLE, 28, 15);
    float to = (float)(8 - THUMB_PITCH * first);
    p->scroll = p->scroll == 0.0f ? to : ease(p->scroll, to, 1);
    for (int i = 0; i <= last; i++) {
        int x = (int)((float)(i * THUMB_PITCH) + p->scroll);
        if (x >= 320 || x + 92 <= 0) continue;                              /* (the blitter's coordinates are 10 bits) */
        s2gfx_sprite(l, (i == pos ? SPR_THUMB : SPR_THUMB_DIM) + s2save_track_at(i) - 1, x, THUMB_Y);
    }
    if ((clock & 0x7F) < 0x40) {
        if (first > 0) s2gfx_sprite(l, SPR_ARROW_LEFT, 10, 170);
        if (first + 2 < last) s2gfx_sprite(l, SPR_ARROW_RIGHT, 298, 170);
    }
    to = (float)(THUMB_PITCH * slot + 2);
    p->frame = p->frame == 0.0f ? to : ease(p->frame, to, 0);
    if ((clock & 0x3F) < 0x20) s2gfx_sprite(l, SPR_FRAME, (int)p->frame, FRAME_Y);
}

/* ------------------------------------------------------------------------------------------------ the car page (a stand-in) */

struct car_page {
    struct s2_menu_cfg *cfg;
};

static int cars_of(int team)
{
    return 4 + (s2save_car5_open(team) ? 1 : 0);
}

static int car_key(void *self, uint32_t press)
{
    struct s2_menu_cfg *c = ((struct car_page *)self)->cfg;
    if (press & S2PAGE_ESC) return S2ARC_BACK;
    if (press & S2PAGE_ENTER) return S2ARC_NEXT;
    int dx = ((press & KEY_RIGHT) ? 1 : 0) - ((press & KEY_LEFT) ? 1 : 0), dy = ((press & KEY_DOWN) ? 1 : 0) - ((press & KEY_UP) ? 1 : 0);
    if (dx) {
        c->car = ring(c->car, dx, 1, cars_of(c->team));
        s2sound_menu(SOUND_CAR);
    } else if (dy) {
        c->team = ring(c->team, dy, 1, S2M_TEAMS);
        if (c->car > cars_of(c->team)) c->car = 1;
        s2sound_menu(SOUND_CAR);
    }
    return 0;
}

static void car_draw(void *self, struct blit_list *l, uint32_t tick)
{
    const struct s2_menu_cfg *c = ((struct car_page *)self)->cfg;
    (void)tick;
    s2gfx_sprite(l, SPR_CAR_TITLE, 28, 15);
    s2gfx_sprite(l, SPR_BADGE + c->team - 1, 226, 1);
    s2gfx_sprite(l, c->car == S2M_CARS ? SPR_PLATE_BONUS + c->team - 1 : SPR_PLATE + c->car - 1, 18, 155);
    /* the stand-in for the carousel of 3D cars: the team's name, which car of how many, how to choose */
    char t[24], *e = t;
    *e++ = (char)('0' + c->car);
    *e++ = ' ';
    *e++ = '/';
    *e++ = ' ';
    *e++ = (char)('0' + cars_of(c->team));
    *e = 0;
    s2gfx_text_centred(l, S2G_FONT_TITLE, S2G_WHITE, 2, 62, s2menu_team_name(c->team));
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, 2, 2, 100, t);
    s2gfx_text_centred(l, S2G_FONT_SMALL, 0, 2, 124, "UP / DOWN: TEAM    LEFT / RIGHT: CAR");
}

/* ------------------------------------------------------------------------------------------------ the transmission page */

struct trans_page {
    struct s2_menu_cfg *cfg;
    uint32_t now, t0;
};

static void trans_frame(void *self, uint32_t tick)
{
    ((struct trans_page *)self)->now = tick;
}

static int trans_key(void *self, uint32_t press)
{
    struct trans_page *p = self;
    if (press & S2PAGE_ESC) return S2ARC_BACK;
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_TRANS);
        return S2ARC_NEXT;
    }
    if ((press & KEY_LEFT) && p->cfg->manual) p->cfg->manual = 0;
    else if ((press & KEY_RIGHT) && !p->cfg->manual) p->cfg->manual = 1;
    else return 0;
    p->t0 = p->now;
    s2sound_menu(SOUND_CURSOR);
    return 0;
}

static void trans_draw(void *self, struct blit_list *l, uint32_t tick)
{
    const struct trans_page *p = self;
    int manual = p->cfg->manual != 0, lit = !(((tick - p->t0) * 2) & 0x20);
    s2gfx_sprite(l, SPR_TRANS_TITLE, 28, 15);
    s2gfx_sprite(l, SPR_BADGE + p->cfg->team - 1, 226, 1);
    s2gfx_sprite(l, SPR_AUTO + (!manual && lit), 18, 155);
    s2gfx_sprite(l, SPR_MANUAL + (manual && lit), 159, 155);
}

/* ------------------------------------------------------------------------------------------------ the pages */

int s2arcade_page(int page, struct s2_menu_cfg *c)
{
    static const char *const picture[S2ARC_PAGES] = { "ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK", "BKG_2L.BKK" };
    if (page < 0 || page >= S2ARC_PAGES || !s2gfx_ready() || !load_picture(picture[page])) return S2ARC_FAIL;
    struct s2_page pg = { s2gfx_pic(0), pal, 0, FADE_FRAMES, 0, 0, 0 };
    struct track_page tp = { c, 0, s2arcade_tracks_open(), 0.0f, 0.0f, 0, 0 };
    struct car_page cp = { c };
    struct trans_page xp = { c, 0, 0 };
    void *self = 0;
    switch (page) {
    case S2ARC_TITLE:
        pg.key = title_key;
        pg.draw = title_draw;
        break;
    case S2ARC_TRACK:
        for (int i = 0; i < tp.n; i++)
            if (s2save_track_at(i) == c->track) tp.pos = i;
        c->track = s2save_track_at(tp.pos);                 /* (a track that is not open: the first) */
        pg.frame = track_frame;
        pg.key = track_key;
        pg.draw = track_draw;
        self = &tp;
        break;
    case S2ARC_CAR:
        if (c->car > cars_of(c->team)) c->car = 1;
        pg.fade_out = 0;
        pg.key = car_key;
        pg.draw = car_draw;
        self = &cp;
        break;
    default:
        pg.frame = trans_frame;
        pg.key = trans_key;
        pg.draw = trans_draw;
        self = &xp;
        break;
    }
    return s2page_run(&pg, self);
}

int s2arcade_run(struct s2_menu_cfg *c)
{
    int page = S2ARC_TITLE;
    for (;;) {
        int r = s2arcade_page(page, c);
        if (r == S2ARC_FAIL) {
            if (page == S2ARC_TITLE) return 0;
            r = S2ARC_NEXT;                                 /* (a page whose picture is missing is left out) */
        }
        if (r == S2ARC_NEXT) {
            if (page == S2ARC_TRANS) return S2ARC_NEXT;
            page++;
        } else {
            if (page <= S2ARC_TRACK) return S2ARC_BACK;
            page--;
        }
    }
}
