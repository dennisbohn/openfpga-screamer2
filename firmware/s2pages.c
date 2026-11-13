/*
 * The GAME SETTINGS and HI SCORES pages (s2pages.h)
 */
#include "s2pages.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2load.h"
#include "s2save.h"
#include "s2sound.h"
#include "s2menumusic.h"
#include "dlog.h"
#include "console.h"
#include "hw.h"
#include <string.h>

#define FADE_OUT    25                  /* 0.5 s: the original's fade to black adds 2.0 a second (VIEW HIGH SCORES fades; ESC is a cut) */
#define SMALL       S2G_FONT_SMALL      /* the original's fonts 2 and 3 of the menu (12 pixels) */
#define SMALL_HOT   S2G_FONT_SMALL2
#define WHITE       1
#define HOT         3                   /* the blinking colour */
#define BLINK_BIT   0x20                /* of the original's hundredths clock (0x4cfd6) */

/* the built-in English texts, the same as STRINGS.TXT's (the page reads the file's lines where it can) */
static const char *const label[9] = { "TRANSMISSION", "DETAIL LEVEL", "NUMBER OF LAPS", "ARCADE ENEMIES SKILL", "MULTIPLAYER ENEMIES",
                                      "TIME ATTACK GHOST CAR", "DAMAGES", "MUSIC", "VIEW HIGH SCORES" };
static const char *const value[20] = { ":AUTO", ":MANUAL", ":LOW", ":MEDIUM", ":HIGH", ":1", ":3", ":5", ":10", ":ROOKIE", ":NORMAL", ":PRO",
                                       ":OFF", ":ON", ":ON", ":OFF", ":OFF", ":ON", ":ON", ":OFF" };
static const uint8_t value_first[8] = { 0, 2, 5, 9, 12, 14, 16, 18 };       /* where each row's values start in the list */

/* ------------------------------------------------------------------------------------------------ GAME SETTINGS */

/* The page's rows: settings of the save (S2S_TRANS + k, STRINGS.TXT's lines in that order), then VIEW HIGH SCORES. The original has all eight; the core leaves DETAIL LEVEL
   out (it draws the H sets only, the row did nothing; the save keeps the value) and closes the list up. The PC tests that lay the page beside the original's build with every
   row (-DS2_SETTINGS_ORIGINAL: tools/menu_screens.sh, tools/room.sh). */
#ifdef S2_SETTINGS_ORIGINAL
static const uint8_t setting_of[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#else
static const uint8_t setting_of[] = { 0, 2, 3, 4, 5, 6, 7 };
#endif
#define SETTINGS    ((int)sizeof setting_of)
#define ROWS        (SETTINGS + 1)      /* and VIEW HIGH SCORES */
#define ROW_Y0      0x30                /* the rows' y: 48, then 12 x 1.2 = 14.4 more each, cut to whole pixels */
#define ROW_STEP    14
#define LABEL_X     10
#define VALUE_X     200

struct settings { int cursor; uint32_t blink; };

enum { LEAVE = -1, SCORES = 1 };           /* (a negative result leaves at once, without the fade out) */

static void set_frame(void *self, uint32_t tick)
{
    (void)tick;
    ((struct settings *)self)->blink += 2;              /* 100 a second, a frame is 20 ms */
}

static void step(int row, int d)
{
    const int k = S2S_TRANS + setting_of[row];
    int n = s2save_count(k);
    s2save_set(k, (s2save_get(k) + d + n) % n);
    s2sound_menu(S2PAGE_SND_ROW);
    if (k == S2S_MUSIC) s2mm_song();                    /* 0x42451 / 0x42462: the song stops, or begins from its start */
}

static int set_key(void *self, uint32_t press)
{
    struct settings *s = self;
    if (press & S2PAGE_ESC) return LEAVE;               /* (silent, as the original's) */
    int d = ((press & KEY_DOWN) ? 1 : 0) - ((press & KEY_UP) ? 1 : 0);
    if (d && s->cursor + d >= 0 && s->cursor + d < ROWS) {
        s->cursor += d;
        s->blink = 0;                                   /* the value lights at once */
        s2sound_menu(S2PAGE_SND_CURSOR);
    }
    if (press & S2PAGE_ENTER) {
        if (s->cursor == ROWS - 1) {
            s2sound_menu(S2PAGE_SND_BACK);
            return SCORES;
        }
        step(s->cursor, 1);
    } else if ((press & (KEY_LEFT | KEY_RIGHT)) && s->cursor < ROWS - 1) {
        step(s->cursor, (press & KEY_RIGHT) ? 1 : -1);
    }
    return 0;
}

/* The title, the nine rows: label at x 10, value at x 200 (in the small font; under the cursor, while it blinks, in the other small font
   and the lit colour with the colon again over it in the plain one), VIEW HIGH SCORES lit as a whole */
static void set_draw(void *self, struct blit_list *l, uint32_t tick)
{
    const struct settings *s = self;
    (void)tick;
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("GAME SETTINGS", 0, "GAME SETTINGS"));
    for (int i = 0; i < ROWS; i++) {
        int y = ROW_Y0 + ROW_STEP * i, lit = s->cursor == i && !(s->blink & BLINK_BIT);
        const int r = i == ROWS - 1 ? 8 : setting_of[i];        /* the original's row: its label and its values */
        const char *name = s2gfx_line("DETAIL LEVEL", r - 1, label[r]);
        if (i == ROWS - 1) {
            s2gfx_text(l, lit ? SMALL_HOT : SMALL, lit ? HOT : WHITE, 2, LABEL_X, y, name);
            continue;
        }
        s2gfx_text(l, SMALL, WHITE, 2, LABEL_X, y, name);
        int k = value_first[r] + s2save_get(S2S_TRANS + r);
        const char *text = s2gfx_line(":AUTO", k, value[k]);
        if (lit) {
            s2gfx_text(l, SMALL_HOT, HOT, 2, VALUE_X, y, text);
            s2gfx_text(l, SMALL, WHITE, 2, VALUE_X, y, ":");
        } else {
            s2gfx_text(l, SMALL, WHITE, 2, VALUE_X, y, text);
        }
    }
}

void s2_page_settings(void)
{
    static uint32_t pal[256];
    for (;;) {
        if (!s2gfx_bkk("BKG_7L.BKK", s2gfx_pic(0), pal)) {
            dlog_str("ui: BKG_7L.BKK not usable\n");
            return;
        }
        struct settings s = { 0, 0 };
        const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, set_frame, set_key, set_draw };
        if (s2page_run(&page, &s) != SCORES) return;
        s2_page_hiscores();                             /* and back to the settings, the cursor at the top */
    }
}

/* ------------------------------------------------------------------------------------------------ HI SCORES */

#define W_LIST      0                   /* the boxes, left to right and top to bottom: TOTAL TIME / LAP TIME, the kind of game, the laps, the track */
#define W_KIND      1
#define W_LAPS      2
#define W_TRACK     3
#define W_REPLAY    4
#define WIDGETS     5
#define LIST_ROWS   S2S_ROWS
#define LIST_Y      120                 /* the first row of the list; the list shows rows between y 119 / 121 and 180 */
#define ROW_H       12
#define VIEW_ROWS   6

static const char *const kind_text[3] = { "ARCADE", "CHAMPIONSHIP", "TIME ATTACK" };
static const char *const laps_text[4] = { "1 LAP", "3 LAPS", "5 LAPS", "10 LAPS" };
static const char *const track_text[7] = { "ENGLAND", "COLUMBIA", "EGYPT", "CALIFORNIA", "FINLAND", "SWITZERLAND", "THE TUNNEL" };
static const uint8_t kind_of[3] = { S2S_KIND_ARCADE, S2S_KIND_LEAGUE, S2S_KIND_ATTACK };    /* the box's kind -> the save's */

/* what the page keeps between its visits (the original keeps the four in its settings block; here until the power goes) */
static struct { int init, lap_list, kind, laps, pos; } sel;

struct scores { int row, widget; uint32_t blink; uint8_t dark[256]; };

static int tracks_open(void)
{
    int n = 0;
    while (n < S2S_TRACKS && s2save_track_open(s2save_track_at(n))) n++;
    return n > 0 ? n : 1;
}

/* the list shown: the save's list number for the boxes' choice */
static const struct s2save_row *list_row(int row)
{
    int list = sel.lap_list ? S2S_LAP : sel.laps;
    return s2save_row(kind_of[sel.kind], s2save_track_at(sel.pos), list, row);
}

static void hs_frame(void *self, uint32_t tick)
{
    (void)tick;
    ((struct scores *)self)->blink += 2;
}

static int hs_key(void *self, uint32_t press)
{
    struct scores *s = self;
    if (press & S2PAGE_ESC) return LEAVE;
    if ((press & KEY_UP) && s->row > 0) s->row--;
    if ((press & KEY_DOWN) && s->row < LIST_ROWS - 1) s->row++;
    if ((press & KEY_LEFT) && s->widget > 0) {
        s->widget--;
        s->blink = BLINK_BIT;
    }
    if ((press & KEY_RIGHT) && s->widget < WIDGETS - 1) {
        s->widget++;
        s->blink = BLINK_BIT;
    }
    if (press & S2PAGE_ENTER) {
        switch (s->widget) {
        case W_LIST: sel.lap_list = !sel.lap_list; break;
        case W_KIND: sel.kind = (sel.kind + 1) % 3; break;
        case W_LAPS:
            if (sel.kind < 2 && !sel.lap_list) sel.laps = (sel.laps + 1) % S2S_LAP;     /* a time attack and the best laps have no length to choose */
            break;
        case W_TRACK: sel.pos = (sel.pos + 1) % tracks_open(); break;
        default: break;                                 /* VIEW REPLAY: no replays yet */
        }
    }
    if (sel.kind >= 2) sel.laps = 1;                    /* time attack has only the 3-lap list */
    return 0;
}

static void hs_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct scores *s = self;
    (void)tick;
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("HI SCORES TABLE", 0, "HI SCORES TABLE"));
    /* the five boxes: a lit one is the other small font in the lit colour, while its half second is on */
    for (int w = 0; w < WIDGETS; w++) {
        int lit = (s->blink & BLINK_BIT) && s->widget == w, font = lit ? SMALL_HOT : SMALL, colour = lit ? HOT : WHITE;
        int x0 = w & 1 ? 160 : 0, y = w < 2 ? 0x2c : 0x41;
        const char *text;
        switch (w) {
        case W_LIST: text = s2gfx_line("HI SCORES TABLE", 1 + sel.lap_list, sel.lap_list ? "LAP TIME" : "TOTAL TIME"); break;
        case W_KIND: text = s2gfx_line("HI SCORES TABLE", 3 + sel.kind, kind_text[sel.kind]); break;
        case W_LAPS: text = s2gfx_line("HI SCORES TABLE", 6 + sel.laps, laps_text[sel.laps]); break;
        case W_TRACK: text = s2gfx_line("HI SCORES TABLE", 10 + s2save_track_at(sel.pos) - 1, track_text[s2save_track_at(sel.pos) - 1]); break;
        default:
            s2gfx_text_centred(l, font, colour, 2, 0x55, s2gfx_line("HI SCORES TABLE", 17, "VIEW REPLAY"));
            continue;
        }
        s2gfx_text_boxed(l, font, colour, 2, x0, 160, y, text);
    }
    /* the list: from the row above the one before the chosen (the chosen is the third), five rows in view between y 119 and 180 */
    int first = s->row > 11 ? 10 : s->row < 3 ? 0 : s->row - 2;
    s2gfx_clip(0x79, 0xB4, 0x77, 0xB4);
    for (int i = first, y = LIST_Y; i < first + VIEW_ROWS && i < LIST_ROWS; i++, y += ROW_H) {
        const struct s2save_row *r = list_row(i);
        char t[40];
        s2_row_text(t, i, r);
        int chosen = i == s->row;
        s2gfx_text(l, chosen ? SMALL_HOT : SMALL, chosen ? HOT : WHITE, 2, 50, y, t);
        s2gfx_sprite(l, S2G_SPR_CAR + (r->team - 1) * 5 + (r->car - 1), 134, y - 1);
    }
    s2gfx_clip(0, FB_H, 0, FB_H);
    /* VIEW REPLAY is dark unless the chosen row has a replay: its box darkened three times, over the text, as the original's 0x51868 */
    if (!list_row(s->row)->replay) s2gfx_dark_after(0x21, 0x51, 0x102, 0x12, 3, s->dark);
}

void s2_page_hiscores(void)
{
    static uint32_t pal[256];
    static struct scores s;
    if (!s2gfx_bkk("HISCOREL.BKK", s2gfx_pic(0), pal)) {
        dlog_str("ui: HISCOREL.BKK not usable\n");
        return;
    }
    if (!sel.init) {                                    /* the first visit: the arcade totals of the race the menu is set for */
        sel.init = 1;
        sel.laps = s2save_get(S2S_LAPS);
        for (int n = 0; n < S2S_TRACKS; n++)
            if (s2save_track_at(n) == s2save_get(S2S_TRACK)) sel.pos = n;
    }
    if (sel.pos >= tracks_open()) sel.pos = 0;
    s = (struct scores){ 0, 0, BLINK_BIT, { 0 } };
    s2gfx_dark_table(pal, s.dark, 256);
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, hs_frame, hs_key, hs_draw };
    s2page_run(&page, &s);
}

/* ------------------------------------------------------------------------------------------------ MULTIPLAYER MENU */

/* The original's three items are COMBAT MODE, NETWORK GAME, SERIAL LINK. The core shows the first alone (2026-11-13, the owner's wish: a network it never has, and a serial link
   over the Pocket's link port is not built); the PC tests that lay the page beside the original's build with all three (-DS2_SETTINGS_ORIGINAL, as GAME SETTINGS' rows above). */
#ifdef S2_SETTINGS_ORIGINAL
#define MULTI_ITEMS     3
#else
#define MULTI_ITEMS     1
#endif
#define MULTI_Y0        70                  /* 0x3c5d7 ..: the items at y 70, 90, 110 (font 1), the message at 180 (font 3, colour 3) */
#define MULTI_STEP      20
#define MESSAGE_FRAMES  250                 /* 5.0 s: the original's timer is 5.0 and runs down by the frame's time */

#define COMBAT          2                   /* the page's result: COMBAT MODE was chosen */

struct multi { int cursor; uint32_t blink; int message_frames, message; };

static void multi_frame(void *self, uint32_t tick)
{
    (void)tick;
    ((struct multi *)self)->blink += 2;
}

static int multi_key(void *self, uint32_t press)
{
    struct multi *m = self;
    if (press & S2PAGE_ESC) return LEAVE;               /* (a cut back to the options room, silent) */
    if ((press & KEY_UP) && m->cursor > 0) {
        m->cursor--;
        m->blink = 0;                                   /* (the clock is set to 0: the item is out first) */
    }
    if ((press & KEY_DOWN) && m->cursor < MULTI_ITEMS - 1) {
        m->cursor++;
        m->blink = 0;
    }
    if ((press & S2PAGE_ENTER) && m->cursor == 0 && IN_PAD2_THERE()) return COMBAT;      /* COMBAT MODE with a second controller at the Dock: the page ends, combat mode follows (viewer.c) */
    if (press & S2PAGE_ENTER) {                         /* COMBAT MODE without a second controller says so, 5 s long (the tests' other two items: what the original shows when it finds no network and no link) */
        m->message = m->cursor;
        m->message_frames = MESSAGE_FRAMES;
    }
    return 0;
}

/* The title, the items (the one under the cursor is out while bit 0x20 of the clock is clear), the message under them while its timer runs. NETWORK GAME and SERIAL LINK, which
   the tests' page alone has, are answered as the original does when it finds no network and no serial link (0xb68b0 and 0xb4b50 fail: NETWORK INITIALIZATION FAILED, UNABLE TO ESTABLISH CONNECTION). COMBAT MODE is
   the two players' race on the screen's halves (0x39901; viewer.c combat_run): the second player's controller is the Dock's second, and without one the item answers SECOND CONTROLLER
   NEEDED (the core's own words: the original's second player has keys of his own on the keyboard). */
static void multi_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct multi *m = self;
    (void)tick;
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("MULTIPLAYER MENU", 0, "MULTIPLAYER MENU"));
    static const char *const item[3] = { "COMBAT MODE", "NETWORK GAME", "SERIAL LINK" };
    for (int i = 0; i < MULTI_ITEMS; i++)
        if ((m->blink & BLINK_BIT) || m->cursor != i)
            s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, MULTI_Y0 + MULTI_STEP * i, s2gfx_line("MULTIPLAYER MENU", 1 + i, item[i]));
    if (m->message_frames > 0) {
        const char *text = m->message == 1 ? s2gfx_line("MULTIPLAYER MENU", 4, "NETWORK INITIALIZATION FAILED") :
                           m->message == 2 ? s2gfx_line("MULTIPLAYER MENU", 9, "UNABLE TO ESTABLISH CONNECTION") : "SECOND CONTROLLER NEEDED";
        s2gfx_text_centred(l, SMALL_HOT, HOT, 2, 180, text);
        m->message_frames--;
    }
}

int s2_page_multi(void)
{
    static uint32_t pal[256];
    if (!s2gfx_bkk("BKG_6L.BKK", s2gfx_pic(0), pal)) {
        dlog_str("ui: BKG_6L.BKK not usable\n");
        return 0;
    }
    struct multi m = { 0, 0, 0, 0 };
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, multi_frame, multi_key, multi_draw };
    return s2page_run(&page, &m) == COMBAT;
}
