/*
 * The championship's pages (s2leaguepages.h)
 *
 * What the original's pages do, read from STARTL.EXE (tools/dis.sh -f; flat addresses). A page runs `while (!done)`: the picture (0x51cd2), the sprites
 * (0x50dda: surface, x, y, sprite, 0) and texts (0x33294: text, y, font, colour + spacing on the stack, centred on the screen; 0x332f5: text, y, width,
 * x0 + font, colour on the stack, centred in a box), the fade, the frame. Fonts are the menu's (0 titles, 1 the 18 pixels, 2 and 3 the small ones), colour 1
 * white, 3 the blinking one, spacing 2. The clock is the timer's hundredths (0x4cfd6, two a frame), set to 0 where a page begins and where its cursor moves;
 * a page is cut in, ENTER fades it out over 25 frames (2.0 x the frame time added to 0..1 each frame), ESC is a cut.
 *
 *   LEAGUE SELECTION (0x4337d): BKG_3L.BKK; the leagues' cups (MENUL.SPR 44 + n, 125 x 110) two at a time at (19, 39) and (176, 39), the window starting at
 *       the league that keeps the chosen one in view; the chosen one framed by sprite 48 at (12 + 157 w, 32) while (clock & 0x3f) < 0x20; for a league that is
 *       not open NOT / AVAILABLE (STRINGS.TXT 23, 24) in font 2 centred in the cup's half of the screen (boxes of 160) at y 100 and 112; below each cup its name and
 *       its number of tracks (LEAGUE 1, 4 TRACKS ...) at y 160 and 172; the other cup (x 18 or 175, y 35, 134 x 156) darkened once; the arrows (sprites 3 and 4, at
 *       (10, 172) and (298, 172)) where there are more leagues to the left or right, while (clock & 0x7f) < 0x40; the title (font 0) at y 2.
 *       LEFT / RIGHT move without wrapping (sound 0, the clock set back); ENTER: on an open league sound 6, fade, on; ESC: back to the options room at once.
 *   WELCOME TO LEAGUE n (0x39569): LEAG_nL.BKK, the league's track plates flying over it (s2plates.h), the top 42 rows darkened once, WELCOME TO (STRINGS.TXT 49) at y 2 and
 *       the league's name at y 20 in font 1. The page ends by itself when the last plate has flown out (6.4 s for league 1, 8.5 s for league 4's six: the TRACK page follows
 *       after the same 25-frame fade as for ENTER, 0x3976a); ENTER: fade, on; ESC: back to the selection at once. No sounds.
 *   LEAGUE REPORT (0x43ade): LEAG_nL.BKK, the title LEAGUE REPORT (font 0) at y 4, the four cars by points (the most first, equal points in the order of the cars: the
 *       player first) in rows 24 apart from y 56: the team's mark (MENUL.SPR 0x51 + team) at x 104 and "n.  NAME  points" centred in font 1 (fixed pitch, one space less
 *       from 10 points up; the name the player's or CPU), then, while races are left, RACES COMPLETED: n at y 152 and RACES TO GO: m at 164 in font 2; ACCELERATE TO
 *       CONTINUE at y 184 (font 2) while (clock & 0x7f) < 0x40. ENTER: with races left fade, the next race; after the last the league is over: the player first
 *       in the standings has won it (the unlocks, s2l_unlock), and the page shows CONGRATULATIONS !!! / YOU'VE WON / LEAGUE n (y 66, 90, 114, font 1) or BETTER LUCK /
 *       NEXT TIME ! (y 66, 90), and the next ENTER fades out to the league selection. (After a win the original goes through the desk scene 0x3bc38 first.)
 *   LEAVING LEAGUE ? ARE YOU SURE ? (the original's TRACK page, 0x3f5b6 .. 0x3f6b7, 0x3f3d3 .. 0x3f4d4): the page darkened once, LEAVING LEAGUE and ARE YOU SURE ?
 *       (font 2, y 80 and 95) and YES / NO at y 115 (the chosen one lit while (clock & 0x3f) <= 0x20); NO is chosen first; LEFT / RIGHT choose, ENTER on YES leaves
 *       (a cut), on NO or ESC the question goes. The original centres it in the left half of its page; here it is in the middle.
 */
#include "s2leaguepages.h"
#include "s2gfx.h"
#include "s2page.h"
#include "s2plates.h"
#include "s2sound.h"
#include "s2save.h"
#include "console.h"
#include "dlog.h"
#include "hw.h"
#include <string.h>

#define FADE_OUT    25
#define SMALL       S2G_FONT_SMALL
#define SMALL_HOT   S2G_FONT_SMALL2
#define WHITE       1
#define HOT         3
#define SOUND_MOVE  0                   /* MENU.DTR ids */
#define SOUND_ENTER 6

enum { SPR_ARROW_LEFT = 3, SPR_ARROW_RIGHT = 4, SPR_CUP = 44, SPR_CUP_FRAME = 48, SPR_MARK = 0x51 };

static uint32_t pal[S2G_PALETTE];
static uint8_t dark[256];

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

static const char *league_picture(int league)
{
    static const char *const name[S2L_LEAGUES] = { "LEAG_1L.BKK", "LEAG_2L.BKK", "LEAG_3L.BKK", "LEAG_4L.BKK" };
    return name[league];
}

/* what is in the list so far is drawn, the box darkened once, and the list is empty again for what comes over it (the original draws in this order) */
static void darken(struct blit_list *l, int x, int y, int w, int h)
{
    const int buffer = l->buffer;
    s2gfx_flush();
    s2gfx_dark_fb(FB(buffer), x, y, w, h, dark);
    blit_start(l, buffer);
}

/* ------------------------------------------------------------------------------------------------ LEAGUE SELECTION */

struct select_page {
    int cursor, first;                  /* the chosen league, the first of the two in view */
    uint32_t now, t0;
};

static void select_frame(void *self, uint32_t tick)
{
    ((struct select_page *)self)->now = tick;
}

static int select_key(void *self, uint32_t press)
{
    struct select_page *p = self;
    if (press & S2PAGE_ESC) return S2LP_BACK;
    if ((press & KEY_LEFT) && p->cursor > 0) {
        p->cursor--;
        p->t0 = p->now;
        s2sound_menu(SOUND_MOVE);
    } else if ((press & KEY_RIGHT) && p->cursor < S2L_LEAGUES - 1) {
        p->cursor++;
        p->t0 = p->now;
        s2sound_menu(SOUND_MOVE);
    }
    if ((press & S2PAGE_ENTER) && s2save_league_open(p->cursor)) {
        s2sound_menu(SOUND_ENTER);
        return S2LP_GO;
    }
    return 0;
}

static void select_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct select_page *p = self;
    while (p->cursor - p->first >= 2) p->first++;
    while (p->cursor - p->first < 0) p->first--;
    const int slot = p->cursor - p->first, clock = (int)((tick - p->t0) * 2);
    s2gfx_sprite(l, SPR_CUP + p->first, 19, 39);
    s2gfx_sprite(l, SPR_CUP + p->first + 1, 176, 39);
    if ((clock & 0x3F) < 0x20) s2gfx_sprite(l, SPR_CUP_FRAME, 12 + 157 * slot, 32);
    for (int i = 0; i < 2; i++) {
        const int league = p->first + i;
        if (!s2save_league_open(league)) {
            s2gfx_text_boxed(l, SMALL, WHITE, 2, 160 * i, 160, 100, s2gfx_line("NOT", 0, "NOT"));
            s2gfx_text_boxed(l, SMALL, WHITE, 2, 160 * i, 160, 112, s2gfx_line("NOT", 1, "AVAILABLE"));
        }
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 160 * i, 160, 160, s2gfx_line("LEAGUE 1", 2 * league, "LEAGUE"));
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 160 * i, 160, 172, s2gfx_line("LEAGUE 1", 2 * league + 1, "TRACKS"));
    }
    darken(l, (slot ? 0 : 157) + 18, 35, 134, 156);                     /* the cup that is not chosen */
    if ((clock & 0x7F) < 0x40) {
        if (p->first != 0) s2gfx_sprite(l, SPR_ARROW_LEFT, 10, 172);
        if (p->first < 2) s2gfx_sprite(l, SPR_ARROW_RIGHT, 298, 172);
    }
    s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 2, s2gfx_line("LEAGUE SELECTION", 0, "LEAGUE SELECTION"));
}

int s2lp_select(int *league)
{
    if (!picture("BKG_3L.BKK")) return S2LP_FAIL;
    struct select_page p = { *league < 0 ? 0 : *league >= S2L_LEAGUES ? S2L_LEAGUES - 1 : *league, 0, 0, 0 };
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, select_frame, select_key, select_draw };
    int r = s2page_run(&page, &p);
    *league = p.cursor;
    return r;
}

/* ------------------------------------------------------------------------------------------------ WELCOME TO LEAGUE n */

static int plates_on, plates_over;      /* the page's plates are loaded and flying; the flight is over (the page ends by itself) */

static void welcome_frame(void *self, uint32_t tick)
{
    (void)self;
    (void)tick;
    if (plates_on && s2plates_step()) plates_over = 1;
}

static int welcome_key(void *self, uint32_t press)
{
    (void)self;
    if (press & S2PAGE_ESC) return S2LP_BACK;
    return (press & S2PAGE_ENTER) || plates_over ? S2LP_GO : 0;
}

static void welcome_draw(void *self, struct blit_list *l, uint32_t tick)
{
    const int league = *(int *)self;
    (void)tick;
    if (plates_on) s2plates_draw(l);                                    /* (over the picture, under the band) */
    darken(l, 0, 0, 320, 42);
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 2, s2gfx_line("WELCOME TO", 0, "WELCOME TO"));
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 20, s2gfx_line("LEAGUE 1", 2 * league, "LEAGUE"));
}

int s2lp_welcome(int league)
{
    if (league < 0 || league >= S2L_LEAGUES || !picture(league_picture(league))) return S2LP_FAIL;
    plates_over = 0;
    plates_on = s2plates_load(s2gfx_zip());                             /* (without them the page waits for ENTER, as it did before the plates were built) */
    if (plates_on) s2plates_begin(s2l_def[league].races);
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, welcome_frame, welcome_key, welcome_draw };
    return s2page_run(&page, &league);
}

/* ------------------------------------------------------------------------------------------------ LEAGUE REPORT */

enum { ST_TABLE, ST_WON, ST_LOST };
#define LEAVE_NOW   (-2)                /* (a cut, like the original's ESC) */

struct report_page {
    const struct s2_league *L;
    int state, asking, yes;             /* the page's state, LEAVING LEAGUE? asked, YES chosen */
    int order[S2L_CARS];
    uint32_t now, t0;
};

static void report_frame(void *self, uint32_t tick)
{
    ((struct report_page *)self)->now = tick;
}

static int report_key(void *self, uint32_t press)
{
    struct report_page *p = self;
    const struct s2_league *L = p->L;
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
    if ((press & S2PAGE_ESC) && p->state == ST_TABLE && !s2l_over(L) && L->races_done > 0) {
        p->asking = 1;
        p->yes = 0;
        p->t0 = p->now;
        return 0;
    }
    if (!(press & S2PAGE_ENTER)) return 0;
    if (p->state == ST_TABLE) {
        if (!s2l_over(L)) return S2LP_NEXT;
        if (s2l_won(L)) {                                               /* the league is won: what it opens (0x44194) */
            s2l_unlock(L);
            p->state = ST_WON;
        } else {
            p->state = ST_LOST;
        }
        return 0;
    }
    return S2LP_DONE;
}

/* the player's name as the original prints it: a string, so no padding spaces */
static void name_of(int car, char out[4])
{
    if (car) {
        memcpy(out, "CPU", 4);
        return;
    }
    const char *n = s2save_name();
    int len = 3;
    while (len > 0 && n[len - 1] == ' ') len--;
    memcpy(out, n, (size_t)len);
    out[len] = 0;
}

static void report_draw(void *self, struct blit_list *l, uint32_t tick)
{
    struct report_page *p = self;
    const struct s2_league *L = p->L;
    const int clock = (int)((tick - p->t0) * 2);
    if (p->state == ST_TABLE) {
        s2gfx_text_centred(l, S2G_FONT_TITLE, WHITE, 2, 4, s2gfx_line("LEAGUE REPORT", 0, "LEAGUE REPORT"));
        for (int k = 0; k < S2L_CARS; k++) {
            const int car = p->order[k], y = 56 + 24 * k, points = L->points[car];
            char name[4], row[32], *s = row;
            name_of(car, name);
            *s++ = 1;
            *s++ = (char)('1' + k);
            *s++ = '.';
            *s++ = ' ';
            *s++ = ' ';
            for (const char *c = name; *c; c++) *s++ = *c;
            *s++ = ' ';
            if (points < 10) *s++ = ' ';
            s = fmt_udec(s, (uint32_t)points);
            *s = 0;
            s2gfx_sprite(l, SPR_MARK + L->pick[car].team - 1, 104, y);
            s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, y, row);
        }
        const int left = s2l_def[L->league].races - L->races_done;
        if (left) {
            char t[40], *s = fmt_str(t, s2gfx_line("RACES COMPLETED:", 0, "RACES COMPLETED:"));
            *s++ = ' ';
            fmt_udec(s, (uint32_t)L->races_done);
            s2gfx_text_centred(l, SMALL, WHITE, 2, 152, t);
            s = fmt_str(t, s2gfx_line("RACES COMPLETED:", 1, "RACES TO GO:"));
            *s++ = ' ';
            fmt_udec(s, (uint32_t)left);
            s2gfx_text_centred(l, SMALL, WHITE, 2, 164, t);
        }
    } else if (p->state == ST_WON) {
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 66, s2gfx_line("CONGRATULATIONS !!!", 0, "CONGRATULATIONS !!!"));
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 90, s2gfx_line("CONGRATULATIONS !!!", 1, "YOU'VE WON"));
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 114, s2gfx_line("LEAGUE 1", 2 * L->league, "LEAGUE"));
    } else {
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 66, s2gfx_line("BETTER LUCK", 0, "BETTER LUCK"));
        s2gfx_text_centred(l, S2G_FONT_MEDIUM, WHITE, 2, 90, s2gfx_line("BETTER LUCK", 1, "NEXT TIME !"));
    }
    if (p->asking) {
        darken(l, 0, 0, 320, 200);
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 80, 160, 80, s2gfx_line("LEAVING LEAGUE", 0, "LEAVING LEAGUE"));
        s2gfx_text_boxed(l, SMALL, WHITE, 2, 80, 160, 95, s2gfx_line("LEAVING LEAGUE", 1, "ARE YOU SURE ?"));
        const int blink = (clock & 0x3F) <= 0x20;
        const int yes_on = p->yes && blink, no_on = !p->yes && blink;
        s2gfx_text_boxed(l, yes_on ? SMALL_HOT : SMALL, yes_on ? HOT : WHITE, 2, 80, 80, 115, s2gfx_line("LEAVING LEAGUE", 2, "YES"));
        s2gfx_text_boxed(l, no_on ? SMALL_HOT : SMALL, no_on ? HOT : WHITE, 2, 160, 80, 115, s2gfx_line("LEAVING LEAGUE", 3, "NO"));
    } else if ((clock & 0x7F) < 0x40) {
        s2gfx_text_centred(l, SMALL, WHITE, 2, 184, s2gfx_line("ACCELERATE TO CONTINUE", 0, "ACCELERATE TO CONTINUE"));
    }
}

int s2lp_report(const struct s2_league *L)
{
    if (L->league < 0 || L->league >= S2L_LEAGUES || !picture(league_picture(L->league))) return S2LP_FAIL;
    struct report_page p = { .L = L, .state = ST_TABLE };
    s2l_standings(L, p.order);
    const struct s2_page page = { s2gfx_pic(0), pal, 0, FADE_OUT, report_frame, report_key, report_draw };
    int r = s2page_run(&page, &p);
    return r == LEAVE_NOW ? S2LP_LEAVE : r;
}
