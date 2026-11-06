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
 *   CAR of the championship and of time attack (S2ARC_LEAGUE, S2ARC_ATTACK; the original's page 0x3cc2c, read from the program, a 3D scene of the team's room where this has the arcade
 *                           page's picture and cars; not seen running in this session): state 0 the cars as above, LEFT / RIGHT turn the ring (sound 11, 0x3d47a, 0x3d562; the title stays CAR
 *                           selection); ENTER (sound 6) goes on; ESC goes back one state (from the first state to TRACK SELECTION / the league's TRACK page). State 1, for a car of the first four
 *                           (the bonus car has none and goes on to the choice, in the championship to the race): the set-up list Standard, Custom 1 .. 4 (STRINGS.TXT) in a box (x 100, y 84,
 *                           120 x 68) darkened through the picture's table, five lines of 12 from y 88, centred, in font 2 colour 1, the chosen one in font 3 colour 3 while bit 0x3f of the
 *                           hundredths since the last move is under 0x20 (lit 0.32 s, dim 0.32 s; always lit in state 2); UP / DOWN move it (sound 8, not round); for a Custom the panel of its six
 *                           numbers (MENUL.SPR 49 at (255, 53) with 0x49 + the number at (273, 76) handling, (301, 76) brakes, 0x4e + the tyre at (273, 108), (301, 108) pressure, (273, 140)
 *                           front, (301, 140) rear). ENTER (sound 6): the championship's Standard goes to the race, any other goes to state 2. State 2: the choice at y 155 (STRINGS.TXT, font 2
 *                           colour 1, the chosen one in font 3 colour 3 while bit 0x20 of that clock is clear): the championship's RACE and CUSTOMIZE in halves of the screen; time attack's
 *                           3 LAPS and FREE RUN in halves, for a Custom with CUSTOMIZE in thirds (106 wide from x 0, 106, 212); LEFT / RIGHT choose (sound 8, not round), ENTER takes it
 *                           (c->free_run; CUSTOMIZE: the page fades out as for the race and the CAR CUSTOMIZATION page follows, then the car page again in state 1 with the same car and set-up).
 *                           Every way out fades the page out over 13 frames (0.08 a frame, 0x3e056), ESC from the cars too (to the TRACK page). The plate, the speed and the drive are as above (a stand-in: the original has them in all states and 10 pixels lower).
 *   TRANSMISSION (state 1): the same picture and cars, sprite 39 "select TRANSMISSION" at (28, 15), the badge, AUTOMATIC at (18, 155) and MANUAL at
 *                           (159, 155): sprites 40 / 42 dim, 41 / 43 lit; the chosen one is lit while (clock & 0x20) == 0, the other is
 *                           always dim. LEFT / RIGHT choose (sound 0, the clock set back); ENTER: sound 3, fade, the race; ESC: back to the CAR state
 *                           (no sound). s2arcade_page(S2ARC_TRANS) is the page started in this state with the cars settled, and its ESC leaves it (the tests').
 */
#include "s2arcade.h"
#include "s2custom.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2sound.h"
#include "s2save.h"
#include "s2showroom.h"
#include "dlog.h"

#define FADE_FRAMES     25
#define SETS_FADE       13                          /* the CAR page of the championship and time attack fades out at 0.08 a frame (STARTL.EXE 0x3e056: dt x 4), on every way out, ESC too */
#define SOUND_CURSOR    0                   /* MENU.DTR ids (MENU1 .. MENU15 are 0 .. 14) */
#define SOUND_TITLE     1
#define SOUND_TRACK     2
#define SOUND_TRANS     3
#define SOUND_CAR       4
#define SOUND_SETS_ENTER    6                       /* the CAR page of the championship and time attack (0x3cc2c: ENTER 6 at 0x3d2e7, the cars 11, the list and the choice 8) */
#define SOUND_SETS_MOVE     8
#define SOUND_SETS_CARS     11
#define S2ARC_CUSTOM    2                           /* what the page ends with when CUSTOMIZE was taken (the page fades out, s2custom.h follows, then the page again) */
#define S2ARC_OUT       3                           /* ... and when ESC left it from the cars (it fades out too, then the result is S2ARC_BACK) */

enum {
    SPR_FRAME = 0, SPR_ARROW_LEFT = 1, SPR_ARROW_RIGHT = 2, SPR_THUMB = 5, SPR_THUMB_DIM = 12,
    SPR_PLATE = 26, SPR_PLATE_BONUS = 30, SPR_CAR_TITLE = 34, SPR_BADGE = 35, SPR_TRANS_TITLE = 39, SPR_AUTO = 40, SPR_MANUAL = 42,
    SPR_PANEL = 49, SPR_DIGIT = 0x49, SPR_TYRE = 0x4e,
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

/* one page in two states (0x40c31: the flag 0x1324c4): the cars on their carousel, then the choice of the gearbox; the CAR pages of the championship and of time attack (`sets`, 0x3cc2c)
   have three: the cars, the set-up Standard / Custom 1 .. 4, the choice (RACE / CUSTOMIZE, 3 LAPS / FREE RUN / CUSTOMIZE) */
struct car_page {
    struct s2_menu_cfg *cfg;
    struct zip *zip;
    int state, n, far_cars, miles, standalone;
    int sets, league;                       /* the page of the championship or of time attack; the championship's */
    int choice;                             /* state 2: the one under the cursor (the original's variable is not set back when the state is entered again) */
    int plate_ok;
    struct s2show_plate plate;
    char speed[8];
    uint8_t dark[256];                      /* the picture's table of darkening: the box of the list */
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
    p->far_cars = s2showroom_step(p->sets ? p->state == 2 : p->state);       /* (the cars glide away for the choice after the list: a stand-in for the original's scene) */
}

/* the bonus car (the fifth) has no set-up list: STARTL.EXE 0x3d2d3 .. 0x3d47a */
static int bonus(const struct s2_menu_cfg *c)
{
    return c->car == S2M_CARS;
}

/* how many the choice of state 2 has: the championship's RACE and CUSTOMIZE; time attack's 3 LAPS and FREE RUN, and CUSTOMIZE for a Custom of a car of the first four (0x3d5e5 .. 0x3d66a) */
static int choices(const struct car_page *p)
{
    return p->league ? 2 : p->cfg->setup != 0 && !bonus(p->cfg) ? 3 : 2;
}

/* the keys of the championship's and time attack's page (STARTL.EXE 0x3d25b ESC, 0x3d2d3 ENTER, 0x3d47a LEFT, 0x3d562 RIGHT, 0x3d522 UP, 0x3d693 DOWN) */
static int sets_key(struct car_page *p, uint32_t press)
{
    struct s2_menu_cfg *c = p->cfg;
    const int five = bonus(c);
    if (press & S2PAGE_ESC) {
        if (!p->state) return S2ARC_OUT;
        p->state = five ? 0 : p->state - 1;
        return 0;
    }
    if (press & S2PAGE_ENTER) {
        s2sound_menu(SOUND_SETS_ENTER);
        if (five) {
            if (p->league) return S2ARC_NEXT;               /* (the bonus car goes to the race: its set-up is the standard) */
            if (!p->state) {
                p->state = 2;
                p->choice = p->choice > 1 ? 1 : p->choice;
                return 0;
            }
        } else if (!p->state) {
            p->state = 1;
            return 0;
        } else if (p->state == 1) {
            if (p->league && !c->setup) return S2ARC_NEXT;  /* (the championship's Standard needs no choice) */
            p->state = 2;
            p->choice = p->choice >= choices(p) ? choices(p) - 1 : p->choice;
            return 0;
        }
        /* the choice is taken */
        if (p->league) return p->choice ? S2ARC_CUSTOM : S2ARC_NEXT;
        if (choices(p) == 3 && p->choice == 2) return S2ARC_CUSTOM;
        c->free_run = p->choice != 0;
        return S2ARC_NEXT;
    }
    if (!p->state) {
        int dx = ((press & KEY_RIGHT) ? 1 : 0) - ((press & KEY_LEFT) ? 1 : 0);
        if (dx && !p->far_cars) {
            s2showroom_move(dx);
            c->car = s2showroom_chosen() + 1;
            plate_numbers(p);
            s2sound_menu(SOUND_SETS_CARS);
            p->t0 = p->now;
        }
    } else if (p->state == 1) {
        if ((press & KEY_UP) && c->setup > 0) c->setup--;
        else if ((press & KEY_DOWN) && c->setup < 4) c->setup++;
        else return 0;
        p->t0 = p->now;
        s2sound_menu(SOUND_SETS_MOVE);
    } else if (((press & KEY_LEFT) && p->choice > 0) || ((press & KEY_RIGHT) && p->choice < choices(p) - 1)) {
        p->choice += (press & KEY_RIGHT) ? 1 : -1;
        p->t0 = p->now;
        s2sound_menu(SOUND_SETS_MOVE);
    }
    return 0;
}

static int car_key(void *self, uint32_t press)
{
    struct car_page *p = self;
    struct s2_menu_cfg *c = p->cfg;
    if (p->sets) return sets_key(p, press);
    if (press & S2PAGE_ESC) {
        if (p->state && !p->standalone) p->state = 0;
        else return S2ARC_BACK;
        return 0;
    }
    if (press & S2PAGE_ENTER) {
        if (!p->state) {
            p->state = 1;
            return 0;
        }
        s2sound_menu(SOUND_TRANS);
        return S2ARC_NEXT;
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

/* the set-up list (0x3db97 ..): the box (100, 84, 120, 68) darkened under it, Standard and Custom 1 .. 4 from y 88 a line of 12; for a Custom the panel of its six numbers (255, 53) */
static void list_draw(const struct car_page *p, struct blit_list *l)
{
    const struct s2_menu_cfg *c = p->cfg;
    for (int i = 0; i < 5; i++) {
        const int lit = i == c->setup && ((((p->now - p->t0) * 2) & 0x3F) < 0x20 || p->state == 2);
        s2gfx_text_centred(l, lit ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, lit ? 3 : S2G_WHITE, 2, 88 + 12 * i, s2gfx_line("Standard", i, i ? "Custom" : "Standard"));
    }
    if (c->setup) {
        const int8_t *six = s2save_setup(c->team, c->car, c->setup - 1);
        s2gfx_sprite(l, SPR_PANEL, 255, 53);
        if (six) {
            s2gfx_sprite(l, SPR_DIGIT + six[0], 273, 76);                   /* handling, brakes, the tyres' letter, pressure, front, rear */
            s2gfx_sprite(l, SPR_DIGIT + six[5], 301, 76);
            s2gfx_sprite(l, SPR_TYRE + six[2], 273, 108);
            s2gfx_sprite(l, SPR_DIGIT + six[1], 301, 108);
            s2gfx_sprite(l, SPR_DIGIT + six[3], 273, 140);
            s2gfx_sprite(l, SPR_DIGIT + six[4], 301, 140);
        }
    }
}

/* the choice (0x3ddca ..): in halves of the screen, or in thirds of 106 for time attack's three; the chosen one lit while bit 0x20 of the clock is clear */
static void choice_draw(const struct car_page *p, struct blit_list *l)
{
    const int n = choices(p), lit_phase = !(((p->now - p->t0) * 2) & 0x20);
    for (int k = 0; k < n; k++) {
        const int lit = lit_phase && p->choice == k;
        const char *text = p->league ? s2gfx_line("RACE", k, k ? "CUSTOMIZE" : "RACE") : k == 2 ? s2gfx_line("RACE", 1, "CUSTOMIZE") : s2gfx_line("3 LAPS", k, k ? "FREE RUN" : "3 LAPS");
        s2gfx_text_boxed(l, lit ? S2G_FONT_SMALL2 : S2G_FONT_SMALL, lit ? 3 : S2G_WHITE, 2, n == 3 ? 106 * k : 160 * k, n == 3 ? 106 : 160, 155, text);
    }
}

static void car_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct car_page *p = self;
    const struct s2_menu_cfg *c = p->cfg;
    const int list = p->sets && p->state && !bonus(c);
    (void)tick;
    if (list) s2showroom_draw_dark(l, p->dark, 100, 84, 120, 68);       /* the picture and the cars, the list's box darkened (the CPU does it before the text goes over it) */
    else s2showroom_draw(l);                                            /* the picture is blitted, the cars drawn; the list is empty again for the sprites */
    s2gfx_sprite(l, p->state && !p->sets ? SPR_TRANS_TITLE : SPR_CAR_TITLE, 28, 15);
    s2gfx_sprite(l, SPR_BADGE + c->team - 1, 226, 1);
    if (list) list_draw(p, l);
    if (p->sets && p->state == 2) {                         /* the choice: where the plate is, which stays off (a stand-in: the original has it in every state, 10 pixels lower) */
        choice_draw(p, l);
    } else if (!p->state || p->sets) {
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

static const char *const picture[S2ARC_PAGES] = { "ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK", "BKG_2L.BKK", "BKG_2L.BKK", "BKG_2L.BKK" };

/* one visit of the CAR page: from the cars, or (after CUSTOMIZE) from the list with the car it left */
static int car_visit(int page, struct s2_menu_cfg *c, int state, int choice)
{
    if (!load_picture(picture[page])) return S2ARC_FAIL;
    struct s2_page pg = { s2gfx_pic(0), pal, 0, FADE_FRAMES, 0, 0, 0 };
    struct car_page cp = { .cfg = c, .zip = s2gfx_zip(), .miles = (s2gfx_line("THIS LAP", 8, "MPH")[0] | 0x20) == 'm' };
    if (c->car > cars_of(c->team)) c->car = 1;
    cp.n = cars_of(c->team);
    cp.sets = page == S2ARC_ATTACK || page == S2ARC_LEAGUE;
    if (cp.sets) pg.fade_out = SETS_FADE;
    cp.league = page == S2ARC_LEAGUE;
    if (cp.sets) c->free_run = 0;
    if (cp.sets && (c->setup < 0 || c->setup > 4)) c->setup = 0;
    cp.state = cp.standalone = page == S2ARC_TRANS;
    if (cp.sets) cp.state = state, cp.choice = choice;
    if (cp.sets) s2gfx_dark_table(pal, cp.dark, 256);       /* (about 70 ms on the Pocket's CPU, with the cars' load before the page is shown; the arcade's page has no box) */
    if (!s2showroom_load(cp.zip, c->team, cp.n)) {
        dlog_str("arcade: the team's cars could not be read: the CAR page without them\n");
        cp.n = 0;
    }
    s2showroom_init(cp.n, c->car - 1);
    if (cp.state == 1 && !cp.sets)
        for (int i = 0; i < 300; i++) s2showroom_step(1);   /* (a page that starts in its second state has the cars settled) */
    plate_numbers(&cp);
    pg.frame = car_frame;
    pg.key = car_key;
    pg.draw = car_draw;
    const int r = s2page_run(&pg, &cp);
    return r == S2ARC_OUT ? S2ARC_BACK : r;
}

int s2arcade_page(int page, struct s2_menu_cfg *c)
{
    if (page < 0 || page >= S2ARC_PAGES || !s2gfx_ready()) return S2ARC_FAIL;
    if (page >= S2ARC_CAR) {                                /* the CAR page, or the TRANSMISSION state of it as a page of its own; CUSTOMIZE leaves and the page comes back to the list */
        int r = car_visit(page, c, 0, 0);
        while (r == S2ARC_CUSTOM) {
            s2custom_page(c, c->setup - 1);                 /* (a Custom of a car of the first four: the choice is only offered for it) */
            r = car_visit(page, c, 1, 0);
        }
        return r;
    }
    if (!load_picture(picture[page])) return S2ARC_FAIL;
    struct s2_page pg = { s2gfx_pic(0), pal, 0, FADE_FRAMES, 0, 0, 0 };
    struct track_page tp = { c, 0, s2arcade_tracks_open(), 0.0f, 0.0f, 0, 0 };
    void *self = 0;
    switch (page) {
    case S2ARC_TITLE:
        pg.key = title_key;
        pg.draw = title_draw;
        break;
    default:
        for (int i = 0; i < tp.n; i++)
            if (s2save_track_at(i) == c->track) tp.pos = i;
        c->track = s2save_track_at(tp.pos);                 /* (a track that is not open: the first) */
        pg.frame = track_frame;
        pg.key = track_key;
        pg.draw = track_draw;
        self = &tp;
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
