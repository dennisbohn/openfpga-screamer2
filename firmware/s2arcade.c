/*
 * The arcade pages (s2arcade.h)
 *
 * What the original's pages do, read from STARTL.EXE (tools/dis.sh -f; flat addresses): the page runs `while (!done)`: the picture, the
 * page's sprites and text, the fade, the frame. A page is cut in (no fade in); ENTER (the keys' code 0x10) fades the screen to black over
 * 25 frames (0.04 a frame: 2.0 x the frame time 0.02; the keys are ignored meanwhile) and ESC (0x20) leaves at once. The blinking is
 * the timer's hundredths (0x4cfd6, 100 a second, two a frame at 50 pictures a second), here tick * 2 from the start of the page (the
 * original's clock runs on from the program's start; it is set back to 0 where a choice changes, as here).
 * Checked against the running original, pixel for pixel: tools/s2/dosbox_arcade.py (the sounds, the fade and the 3D parts are not).
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
 *   CAR (0x40c31, state 0): BKG_2L.BKK, the team's four or five cars in 3D on a carousel (s2showroom.h) turning, the chosen one in front; sprite 34
 *                           "CAR selection" at (28, 15), the team's badge (35 + team) at (226, 1), the car's plate at (18, 155): sprite 26 + car for the
 *                           four cars, 30 + team for the fifth (the names are the sprites' art), sprite 106 (the label "maxspeed:") at (166, 170), the top
 *                           speed as three digits (font 5 colour 5, fixed pitch, mph = CARS.DTR's MAX_SPEED x 1.12 x 1.188 / 1.6, or km/h without the 1.6, when GAME.TXT's
 *                           unit word starts with M) at (213, 169), the unit (sprite 110) at (231, 171) and the drive (sprites 107 front, 108 rear,
 *                           109 four-wheel: from the shares the two left wheels get) at (251, 163). LEFT / RIGHT turn the ring (sound 4; taken only while
 *                           the cars are within 150 of their places); the team was chosen on TEAM SELECTION. ENTER: the same page goes to its second state
 *                           (no sound, no fade; the cars glide away); ESC: back to the TRACK page.
 *   CAR of time attack (S2ARC_ATTACK, the original's 0x3cc2c is another page: a 3D room with the set-ups (Standard, Custom 1 .. 4) between the car and the choice; here the arcade's page):
 *                           the same cars in state 0; ENTER (sound 6) goes to the choice, 3 LAPS at the left and FREE RUN at the right (STRINGS.TXT 114 and 115, centred in the halves of the
 *                           screen at y 155 in font 2, colour 1; the chosen one in font 3, colour 3 while bit 0x20 of the hundredths since the choice moved is clear: 0.32 s lit, 0.32 s
 *                           dim), the title stays CAR selection; LEFT / RIGHT choose (sound 8, 0x3d4f9, 0x3d618), ENTER (sound 6) takes it (c->free_run), ESC goes back to the cars (state 0)
 *                           and from there to TRACK SELECTION.
 *   TRANSMISSION (state 1): the same picture and cars, sprite 39 "select TRANSMISSION" at (28, 15), the badge, AUTOMATIC at (18, 155) and MANUAL at
 *                           (159, 155): sprites 40 / 42 dim, 41 / 43 lit; the chosen one is lit while (clock & 0x20) == 0, the other is
 *                           always dim. LEFT / RIGHT choose (sound 0, the clock set back); ENTER: sound 3, fade, the race; ESC: back to the CAR state
 *                           (no sound). s2arcade_page(S2ARC_TRANS) is the page started in this state with the cars settled, and its ESC leaves it (the tests').
 */
#include "s2arcade.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "s2save.h"
#include "s2showroom.h"
#include "dlog.h"

#define FADE_FRAMES     25
#define SOUND_CURSOR    0                   /* MENU.DTR ids (MENU1 .. MENU15 are 0 .. 14) */
#define SOUND_TITLE     1
#define SOUND_TRACK     2
#define SOUND_TRANS     3
#define SOUND_CAR       4
#define SOUND_ATTACK_ENTER  6                       /* time attack's CAR page (0x3d2e7 ..) */
#define SOUND_ATTACK_MOVE   8

enum {
    SPR_FRAME = 0, SPR_ARROW_LEFT = 1, SPR_ARROW_RIGHT = 2, SPR_THUMB = 5, SPR_THUMB_DIM = 12,
    SPR_PLATE = 26, SPR_PLATE_BONUS = 30, SPR_CAR_TITLE = 34, SPR_BADGE = 35, SPR_TRANS_TITLE = 39, SPR_AUTO = 40, SPR_MANUAL = 42,
    SPR_TRACK_TITLE = 105, SPR_MAXSPEED = 106, SPR_DRIVE = 107, SPR_UNIT = 110
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

/* ------------------------------------------------------------------------------------------------ the car page and the transmission page */

/* one page in two states (0x40c31: the flag 0x1324c4): the cars on their carousel, then the choice of the gearbox */
struct car_page {
    struct s2_menu_cfg *cfg;
    struct zip *zip;
    int state, n, far_cars, miles, standalone, attack;
    int plate_ok;
    struct s2show_plate plate;
    char speed[8];
    uint32_t now, t0;                       /* the frame, and the frame the blinking began at (the clock is set back where a choice changes) */
};

static int cars_of(int team)
{
    return 4 + (s2save_car5_open(team) ? 1 : 0);
}

/* the plate's numbers for the chosen car (0x41766): the car's MAX_SPEED made into its record with the standard set-up (0x14e20: x 1.12, the tyre and pressure terms are 0 for
   2 2 0 2 2 2), x 1.188 / 1.6 as miles an hour or x 1.188, cut, three digits, fixed pitch (HORIZON: 144.5 gives 120; the running original shows 120) */
static void plate_numbers(struct car_page *p)
{
    p->plate_ok = s2showroom_plate(p->zip, p->cfg->team, p->cfg->car, &p->plate);
    int v = (int)((double)(p->plate.max_speed * 1.12f) * 1.188 / (p->miles ? 1.6 : 1.0));
    p->speed[0] = 1;
    p->speed[1] = (char)('0' + v / 100 % 10);
    p->speed[2] = (char)('0' + v / 10 % 10);
    p->speed[3] = (char)('0' + v % 10);
    p->speed[4] = 0;
}

static void car_frame(void *self, uint32_t tick)
{
    struct car_page *p = self;
    p->now = tick;
    p->far_cars = s2showroom_step(p->state);
}

static int car_key(void *self, uint32_t press)
{
    struct car_page *p = self;
    struct s2_menu_cfg *c = p->cfg;
    if (press & S2PAGE_ESC) {
        if (p->state && !p->standalone) p->state = 0;
        else return S2ARC_BACK;
        return 0;
    }
    if (press & S2PAGE_ENTER) {
        if (p->attack) s2sound_menu(SOUND_ATTACK_ENTER);
        if (!p->state) {
            p->state = 1;
            if (p->attack) p->t0 = p->now;
            return 0;
        }
        if (!p->attack) s2sound_menu(SOUND_TRANS);
        return S2ARC_NEXT;
    }
    if (p->attack && p->state) {                            /* 3 LAPS / FREE RUN */
        if ((press & KEY_LEFT) && c->free_run) c->free_run = 0;
        else if ((press & KEY_RIGHT) && !c->free_run) c->free_run = 1;
        else return 0;
        p->t0 = p->now;
        s2sound_menu(SOUND_ATTACK_MOVE);
        return 0;
    }
    if (!p->state) {
        int dx = ((press & KEY_RIGHT) ? 1 : 0) - ((press & KEY_LEFT) ? 1 : 0);
        if (dx && !p->far_cars) {
            s2showroom_move(dx);
            c->car = s2showroom_chosen() + 1;
            plate_numbers(p);
            s2sound_menu(SOUND_CAR);
        }
        return 0;
    }
    if ((press & KEY_LEFT) && c->manual) c->manual = 0;
    else if ((press & KEY_RIGHT) && !c->manual) c->manual = 1;
    else return 0;
    p->t0 = p->now;
    s2sound_menu(SOUND_CURSOR);
    return 0;
}

static void car_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct car_page *p = self;
    const struct s2_menu_cfg *c = p->cfg;
    (void)tick;
    s2showroom_draw(l);                                     /* the picture is blitted, the cars drawn; the list is empty again for the sprites */
    s2gfx_sprite(l, p->state && !p->attack ? SPR_TRANS_TITLE : SPR_CAR_TITLE, 28, 15);
    s2gfx_sprite(l, SPR_BADGE + c->team - 1, 226, 1);
    if (p->attack && p->state) {                            /* 3 LAPS / FREE RUN (0x3de0b ..): in halves of the screen, the chosen one lit while bit 0x20 of the clock is clear */
        const int lit_phase = !(((p->now - p->t0) * 2) & 0x20);
        for (int k = 0; k < 2; k++) {
            const int lit = lit_phase && c->free_run == k;
            s2gfx_text_boxed(l, lit ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, lit ? 3 : S2G_WHITE, 2, 160 * k, 160, 155, s2gfx_line("3 LAPS", k, k ? "FREE RUN" : "3 LAPS"));
        }
    } else if (!p->state) {
        s2gfx_sprite(l, c->car == S2M_CARS ? SPR_PLATE_BONUS + c->team - 1 : SPR_PLATE + c->car - 1, 18, 155);
        s2gfx_sprite(l, SPR_MAXSPEED, 166, 170);
        s2gfx_sprite(l, SPR_UNIT, 231, 171);
        if (p->plate_ok) {
            s2gfx_sprite(l, SPR_DRIVE + (p->plate.drive == 2 ? 1 : p->plate.drive == 1 ? 0 : 2), 251, 163);
            s2gfx_text(l, S2G_FONT_DIGITS, 5, 1, 213, 169, p->speed);
        }
    } else {
        const int manual = c->manual != 0, lit = !(((p->now - p->t0) * 2) & 0x20);
        s2gfx_sprite(l, SPR_AUTO + (!manual && lit), 18, 155);
        s2gfx_sprite(l, SPR_MANUAL + (manual && lit), 159, 155);
    }
}

int s2arcade_page(int page, struct s2_menu_cfg *c)
{
    static const char *const picture[S2ARC_PAGES] = { "ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK", "BKG_2L.BKK", "BKG_2L.BKK" };
    if (page < 0 || page >= S2ARC_PAGES || !s2gfx_ready() || !load_picture(picture[page])) return S2ARC_FAIL;
    struct s2_page pg = { s2gfx_pic(0), pal, 0, FADE_FRAMES, 0, 0, 0 };
    struct track_page tp = { c, 0, s2arcade_tracks_open(), 0.0f, 0.0f, 0, 0 };
    struct car_page cp = { .cfg = c, .zip = s2gfx_zip(), .miles = (s2gfx_line("THIS LAP", 8, "MPH")[0] | 0x20) == 'm' };
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
    default:                                                /* the CAR page, or the TRANSMISSION state of it as a page of its own */
        if (c->car > cars_of(c->team)) c->car = 1;
        cp.n = cars_of(c->team);
        cp.attack = page == S2ARC_ATTACK;
        if (cp.attack) c->free_run = 0;
        cp.state = cp.standalone = page == S2ARC_TRANS;
        if (!s2showroom_load(cp.zip, c->team, cp.n)) {
            dlog_str("arcade: the team's cars could not be read: the CAR page without them\n");
            cp.n = 0;
        }
        s2showroom_init(cp.n, c->car - 1);
        if (cp.state)
            for (int i = 0; i < 300; i++) s2showroom_step(1);   /* (a page that starts in its second state has the cars settled) */
        plate_numbers(&cp);
        pg.frame = car_frame;
        pg.key = car_key;
        pg.draw = car_draw;
        self = &cp;
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
            if (page == S2ARC_CAR) return S2ARC_NEXT;          /* (the CAR page goes through its TRANSMISSION state itself) */
            page++;
        } else {
            if (page <= S2ARC_TRACK) return S2ARC_BACK;
            page--;
        }
    }
}
