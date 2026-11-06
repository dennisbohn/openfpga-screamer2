/*
 * The CAR CUSTOMIZATION page (s2custom.h)
 *
 * Read from STARTL.EXE (tools/dis.sh -f; flat addresses; read from the program and compared with the running original on the PC only through the model of tools/ui_custom_ref.py,
 * which is written from the same reading: the page was not seen running in this session): the page 0x3e1d5 .. 0x3ee27, entered from the CAR page of the championship and of time
 * attack (0x3cc2c) with the car's Custom set-up in hand (the team's record at 0x128a9c + team x 0x188 + 8 + car x 0x60 + (Custom - 1) x 0x18, six words), left for the CAR page
 * again (its state 1, the same car and set-up).
 *
 * The scene: the team's room, a garage with the car on its lift, seen from the camera station of the item under the cursor (s2garage.h: DATA/GEN's P_<team>.MCM, record 2: place and
 * aim of each of seven stations, the car at aim 0 turned a quarter; the camera flies in from the last station and glides from one to the next, 95 % of the way a frame).
 * The 2D parts: MENUL.SPR 49 at (250, 30), the six pictures of the items (two columns, three rows of 28 x 32), and 50 at (250, 128), SAVE and EXIT (two buttons of 56 x 27);
 * the title CAR CUSTOMIZATION (STRINGS.TXT, font 0, colour 1, spacing 2) centred at y 0; the cursor, MENUL.SPR 51 (29 x 34) over the item's picture, 52 (56 x 29) over SAVE and EXIT
 * (the table at 0x113120: x 250 + 27 (item & 1), y 30 + 32 (item >> 1) for the six, (250, 126) and (250, 153)), always drawn; the item's name (STRINGS.TXT, font 1, colour 1,
 * spacing 2) centred at y 180. An item opened (ENTER) slides its picture and scale up: with f (0 .. 1, +0.16 a frame while it is open, -0.16 while it is not: dt x 8) the big picture
 * (MENUL.SPR 0x3d + the item, 45 x 54) is at x 87 f - 45, y 90; the scale (0x35, 88 x 22) at (17, 200 - 50 f); a minus (12 x 6) at (5, 210 - 50 f) and a plus (12 x 12) at
 * (104, 206 - 50 f), dim (0x48, 0x47) or lit (0x3c, 0x3b) for 4 frames after the value went down or up (a flash that is 1 and falls 0.3 a frame: dt x 15); and, while bit 0x3f of the
 * hundredths since the last change is under 0x20, the marker of the value (0x36 + the value, 23 x 22) at (17 + 16 value, 200 - 50 f). The tyres' page: the picture 0x3f,
 * the three tyres (0x43 .. 0x45, 50 x 30) at x 8, 60, 112 and y 200 - 50 f, the name of the tyre (STRINGS.TXT's A - Dry, B - Wet, C - Snow, font 2) at (170, 200 - 42 f) and, blinking,
 * the frame 0x46 at (8 + 52 tyre, 200 - 50 f). Every position is a whole number cut towards zero from the sum in double precision, as the program's frndint does.
 *
 * Keys (0x389ae; the codes 1 up, 2 down, 4 left, 8 right, 0x10 ENTER, 0x20 ESC): the d-pad keys only when the picture is whole in or out (f is 0 or 1). Closed (browsing): LEFT / RIGHT one
 * item back or on (sound 12; RIGHT past the last: none), UP from SAVE and EXIT the item before, from the pictures two before (no sound), DOWN from the pictures two on (the fifth to SAVE),
 * from SAVE to EXIT; ENTER on a picture opens it (sound 4, f restarts at 0, the value is copied), ESC puts the cursor on EXIT. Open: LEFT and RIGHT change the value (sound 0 with every
 * LEFT, with a RIGHT that changed it; the clock of the blinking starts again, the flash of the sign), 0 .. 4 and for the tyres 0 .. 2; ENTER keeps it (sound 4), ESC drops it (sound 4).
 * ENTER on SAVE writes the six numbers into the team's record (sound 7) and leaves; ENTER on EXIT leaves (sound 13). Leaving fades the page out (0.08 a frame: 13 frames).
 */
#include "s2custom.h"
#include "s2garage.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2room.h"
#include "s2sound.h"
#include "s2save.h"
#include "dlog.h"

#define DT              0.02f
#define FADE_FRAMES     13
#define FADE_STEP       2621                    /* 0.08 of the palette a frame (0x514c1), as the CAR page leaves (s2arcade.c) */

enum {
    SPR_CURSOR = 51, SPR_CURSOR_WIDE = 52, SPR_PANEL = 49, SPR_BUTTONS = 50, SPR_STRIP = 0x35, SPR_MARKER = 0x36, SPR_PLUS_LIT = 0x3b, SPR_MINUS_LIT = 0x3c, SPR_ICON = 0x3d,
    SPR_TYRE_PIC = 0x43, SPR_TYRE_FRAME = 0x46, SPR_PLUS = 0x47, SPR_MINUS = 0x48
};

enum { SOUND_VALUE = 0, SOUND_OPEN = 4, SOUND_SAVE = 7, SOUND_ITEM = 12, SOUND_EXIT = 13 };

/* the item's number in the set-up's six (the save's order: handling, pressure, tyres, front, rear, brakes): the page's rows are HANDLING, BRAKES, TIRE TYPE, TIRE PRESSURE, FRONT, REAR */
static const uint8_t slot_of[6] = { 0, 5, 2, 1, 3, 4 };

struct page {
    struct s2_menu_cfg *cfg;
    int slot;                               /* Custom n - 1 */
    int8_t six[6];                          /* the set-up in hand */
    int item, open, value;                  /* the cursor (0 .. 7), a picture open (0x1324c4), the value of the open item (the original's local copy) */
    float f, minus, plus;                   /* the picture's slide 0 .. 1, the flashes of the signs (the program's 1.0 that falls) */
    uint32_t now, t0;                       /* the frame, and the frame the blinking began at */
};

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static const char *const item_name[8] = { "HANDLING", "BRAKES", "TIRE TYPE", "TIRE PRESSURE", "FRONT SUSPENSIONS", "REAR SUSPENSIONS", "SAVE", "EXIT" };
static const char *const tyre_name[3] = { "A - Dry", "B - Wet", "C - Snow" };

static int most(int item)
{
    return item == 2 ? 2 : 4;                  /* the tyres have three, the others five */
}

static void custom_frame(void *self, uint32_t tick)
{
    struct page *p = self;
    p->now = tick;
}

static int custom_key(void *self, uint32_t press)
{
    struct page *p = self;
    const int whole = p->f == 0.0f || p->f == 1.0f;                     /* the d-pad's keys only when the picture is in or out (0x3e67f ..) */
    if (press & S2PAGE_ESC) {
        if (!p->open) {
            p->item = 7;                                                /* (not a way out: the cursor goes to EXIT) */
        } else {
            p->open = 0;                                                /* (the value is dropped) */
            s2sound_menu(SOUND_OPEN);
        }
    } else if (press & S2PAGE_ENTER) {
        if (p->item < 6) {
            const int s = slot_of[p->item];
            s2sound_menu(SOUND_OPEN);
            if (!p->open) {
                p->open = 1;
                p->value = p->six[s];
                p->f = 0.0f;
            } else {
                p->open = 0;
                p->six[s] = (int8_t)p->value;
            }
        } else {
            if (p->item == 6) {
                s2save_set_setup(p->cfg->team, p->cfg->car, p->slot, p->six);
                s2sound_menu(SOUND_SAVE);
            } else {
                s2sound_menu(SOUND_EXIT);
            }
            return 1;
        }
    } else if ((press & KEY_LEFT) && whole) {
        if (!p->open) {
            if (p->item > 0) {
                p->item--;
                s2sound_menu(SOUND_ITEM);
            }
        } else {
            if (p->value > 0) p->value--;
            s2sound_menu(SOUND_VALUE);
            p->t0 = p->now;
            p->minus = 1.0f;
        }
    } else if ((press & KEY_RIGHT) && whole) {
        if (!p->open) {
            if (++p->item >= 8) p->item = 7;
            else s2sound_menu(SOUND_ITEM);
        } else {
            if (p->value < most(p->item)) {
                p->value++;
                s2sound_menu(SOUND_VALUE);
            }
            p->plus = 1.0f;
            p->t0 = p->now;
        }
    } else if ((press & KEY_UP) && whole && !p->open) {
        if (p->item > 5) p->item--;
        else if (p->item > 1) p->item -= 2;
    } else if ((press & KEY_DOWN) && whole && !p->open) {
        if (p->item < 5) p->item += 2;
        else if (p->item < 7) p->item++;
    }
    return 0;
}

static void custom_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct page *p = self;
    (void)tick;
    s2garage_cust_draw(l, p->item);                                    /* the room, the car and the camera at the item's station (the list is empty again for the sprites) */
    s2gfx_sprite(l, SPR_PANEL, 250, 30);
    s2gfx_sprite(l, SPR_BUTTONS, 250, 128);
    s2gfx_text_centred(l, S2G_FONT_TITLE, S2G_WHITE, 2, 0, s2gfx_line("CAR CUSTOMIZATION", 0, "CAR CUSTOMIZATION"));
    if (p->item < 6) s2gfx_sprite(l, SPR_CURSOR, 250 + 27 * (p->item & 1), 30 + 32 * (p->item >> 1));
    else s2gfx_sprite(l, SPR_CURSOR_WIDE, 250, p->item == 6 ? 126 : 153);
    /* the picture's slide (0x3e9e0): +dt x 8 while it is open, -dt x 8 while it is not, within 0 .. 1 */
    p->f = p->open ? p->f + DT * 8.0f : p->f - DT * 8.0f;
    if (p->f > 1.0f) p->f = 1.0f;
    if (p->f < 0.0f) p->f = 0.0f;
    const double f = (double)p->f;
    const int on = ((p->now - p->t0) * 2 & 0x3F) < 0x20;                 /* bit 0x3f of the hundredths since the last change */
    if (p->item != 2) {
        s2gfx_sprite(l, SPR_ICON + (p->item > 5 ? 5 : p->item), (int)(87.0 * f - 45.0), 90);
        s2gfx_sprite(l, SPR_STRIP, 17, (int)(200.0 - 50.0 * f));
        s2gfx_sprite(l, p->minus != 0.0f ? SPR_MINUS_LIT : SPR_MINUS, 5, (int)(210.0 - 50.0 * f));
        s2gfx_sprite(l, p->plus != 0.0f ? SPR_PLUS_LIT : SPR_PLUS, 104, (int)(206.0 - 50.0 * f));
        if (on) s2gfx_sprite(l, SPR_MARKER + p->value, 17 + 16 * p->value, (int)(200.0 - 50.0 * f));
    } else {
        s2gfx_sprite(l, SPR_ICON + 2, (int)(87.0 * f - 45.0), 90);
        for (int k = 0; k < 3; k++) s2gfx_sprite(l, SPR_TYRE_PIC + k, k == 0 ? 8 : k == 1 ? 60 : 112, (int)(200.0 - 50.0 * f));
        s2gfx_text(l, S2G_FONT_SMALL, S2G_WHITE, 2, 170, (int)(200.0 - 42.0 * f), s2gfx_line("A - Dry", p->value, tyre_name[p->value]));
        if (on) s2gfx_sprite(l, SPR_TYRE_FRAME, 52 * p->value + 8, (int)(200.0 - 50.0 * f));
    }
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 180, s2gfx_line("HANDLING", p->item, item_name[p->item]));
    p->minus -= DT * 15.0f;
    if (p->minus < 0.0f) p->minus = 0.0f;
    p->plus -= DT * 15.0f;
    if (p->plus < 0.0f) p->plus = 0.0f;
}

int s2custom_page(struct s2_menu_cfg *c, int slot)
{
    if (!s2gfx_ready() || slot < 0 || slot > 3 || c->car < 1 || c->car > 4) return 0;
    const int n = 4 + (s2save_car5_open(c->team) ? 1 : 0);
    if (!s2garage_open(c->team, n)) {                       /* (the team's room and cars: where the CAR page before this one has them, nothing is read) */
        dlog_str("custom: the team's room or cars could not be had: the page is not shown\n");
        return 0;
    }
    struct page pg = { .cfg = c, .slot = slot };
    const int8_t *six = s2save_setup(c->team, c->car, slot);
    for (int k = 0; k < 6; k++) pg.six[k] = (int8_t)clamp(six ? six[k] : 2, 0, k == 2 ? 2 : 4);        /* (the save holds -1 .. 4: the page's ranges) */
    s2garage_cust_begin(c->car - 1);
    struct s2_page page = { 0, track_palette(), 0, FADE_FRAMES, custom_frame, custom_key, custom_draw };
    s2page_run_step(&page, &pg, FADE_STEP);
    return 1;
}
