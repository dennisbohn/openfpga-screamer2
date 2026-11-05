/*
 * The end of the race (s2end.h)
 */
#include "s2end.h"
#include "s2gfx.h"
#include "s2load.h"
#include "s2page.h"
#include "s2save.h"
#include "s2league.h"
#include "console.h"
#include "dlog.h"
#include "hw.h"
#include <string.h>

#define FONT        S2G_FONT_MEDIUM     /* the race screen's font 0 (GAMEL.FNT, the menu's font 1), its 7 and 8 the menu's 2 and 3 */
#define SMALL       S2G_FONT_SMALL
#define SMALL_HOT   S2G_FONT_SMALL2
#define WHITE       1
#define HOT         3
#define BLINK_BIT   0x20                /* of the original's hundredths clock (0x4cfd6) */
#define CONGRATS_END    400             /* hundredths: four seconds of the first screen */
#define SCROLL_START    450             /* the list starts to move half a second after the choices come */
#define ROWS        S2S_ROWS
#define VIEW        6                   /* rows drawn: five in view and the one coming in */

static struct {
    int started;
    uint32_t last_vf, clock, blink;     /* the clocks in hundredths of a second */
    int choice;                         /* 0 RACE AGAIN, 1 EXIT; with a replay 0 REPLAY, 1 RACE AGAIN, 2 EXIT (the championship's: 0 REPLAY, 1 CONTINUE) */
    int kind;                           /* the lists' kind of game: S2S_KIND_ARCADE, S2S_KIND_LEAGUE, S2S_KIND_ATTACK */
    int recorded, rank;                 /* the times are in the lists; the row the total time took, -1 if none */
    int first;                          /* the first row of the list in view, and how far it is into the move to the next, 0 .. 1 */
    float frac;
    uint8_t dark[256];                  /* the darkening table of the race's palette */
} E;

/* the player's times into the lists, at the moment the original does (the first frame of the choices) */
static void record(const struct s2_end_in *in)
{
    static int logged;
    E.recorded = 1;
    const int kind = E.kind;
    E.rank = s2save_add_time(kind, in->track, s2_laps_list(in->laps), in->time, 0, in->team, in->car, 0);
    if (in->best_lap > 0.0f) s2save_add_time(kind, in->track, S2S_LAP, in->best_lap, 0, in->team, in->car, 0);
    if (logged++ < 3) {                 /* (the log has 4 KB) */
        char t[16];
        s2_time_text(t, in->time);
        dlog_str("end: T");
        dlog_dec(in->track);
        dlog_str(" place ");
        dlog_dec(in->place);
        dlog_str(" ");
        dlog_str(t);
        dlog_str(" rank ");
        dlog_dec(E.rank + 1);
        dlog_char('\n');
    }
}

/* CONGRATULATIONS ! (first place), "1ST POSITION", the total time: the race screen's font 0, colour 1, spacing 2, centred at y 70, 100, 130 */
static void congratulations(struct blit_list *l, const struct s2_end_in *in)
{
    char t[24];
    if (in->attack) {                   /* time attack (0x48b4c): TOTAL and the time, LAP TIME and the best lap */
        s2gfx_text_centred(l, FONT, WHITE, 2, 60, s2gfx_line("TOTAL", 0, "TOTAL"));
        s2_time_text(t, in->time);
        s2gfx_text_centred(l, FONT, WHITE, 2, 80, t);
        s2gfx_text_centred(l, FONT, WHITE, 2, 100, s2gfx_line("LAP TIME", 0, "LAP TIME"));
        s2_time_text(t, in->best_lap);
        s2gfx_text_centred(l, FONT, WHITE, 2, 120, t);
        return;
    }
    static const char *const ord[4] = { "1ST", "2ND", "3RD", "4TH" };
    int place = in->place < 1 ? 1 : in->place > 4 ? 4 : in->place;
    if (in->place == 1) s2gfx_text_centred(l, FONT, WHITE, 2, 70, s2gfx_line("1ST", 5, "CONGRATULATIONS !"));
    char *p = t;
    for (const char *s = s2gfx_line("1ST", place - 1, ord[place - 1]); *s; s++) *p++ = *s;
    *p++ = ' ';
    for (const char *s = s2gfx_line("1ST", 4, "POSITION"); *s && p < t + 22; s++) *p++ = *s;
    *p = 0;
    s2gfx_text_centred(l, FONT, WHITE, 2, 100, t);
    s2_time_text(t, in->time);
    s2gfx_text_centred(l, FONT, WHITE, 2, 130, t);
}

/* the choices over the darkened band at the bottom, the list of best times in its box (the banner first) */
static void choices(struct blit_list *l, const struct s2_end_in *in)
{
    s2gfx_sprite_centred(l, s2gfx_banner(in->track), 4);
    if (in->replay) {                   /* REPLAY, RACE AGAIN, EXIT in thirds: boxes of 106 from x 0, 106 and 212 (0x47a4f ..) */
        for (int k = 0; k < 3; k++) {
            int lit = E.choice == k && (E.blink & BLINK_BIT);
            s2gfx_text_boxed(l, lit ? SMALL_HOT : SMALL, lit ? HOT : WHITE, 2, 106 * k, 106, 184, s2gfx_line("REPLAY", k, k == 0 ? "REPLAY" : k == 1 ? "RACE AGAIN" : "EXIT"));
        }
    } else {
        for (int k = 0; k < 2; k++) {
            int lit = E.choice == k && (E.blink & BLINK_BIT);
            s2gfx_text_boxed(l, lit ? SMALL_HOT : SMALL, lit ? HOT : WHITE, 2, 160 * k, 160, 184, s2gfx_line("RACE AGAIN", k, k ? "EXIT" : "RACE AGAIN"));
        }
    }
    int y0 = (int)(80.0f - 12.0f * E.frac), list = s2_laps_list(in->laps);
    s2gfx_clip(0x51, 0x8C, 0x50, 0x8C);
    for (int i = E.first, y = y0; i < E.first + VIEW && i < ROWS; i++, y += 12) {
        const struct s2save_row *r = s2save_row(E.kind, in->track, list, i);
        char t[40];
        s2_row_text(t, i, r);
        int mine = i == E.rank;
        s2gfx_text(l, mine ? SMALL_HOT : SMALL, mine ? HOT : WHITE, 2, 50, y, t);
        s2gfx_sprite(l, S2G_SPR_CAR + (r->team - 1) * 5 + (r->car - 1), 134, y - 1);
    }
    s2gfx_clip(0, FB_H, 0, FB_H);
}

/* The championship's choices (0x48041 from 0x4842d): the banner, CONTINUE alone, centred at y 184 (lit while the clock's bit 0x20 is set), and the box of the cars
   that have done their laps: n of them, the header at y 81 - 5 (n - 1) from x 38, the rows 12 apart from 14 below it (by place, in the order of the cars), each
   "<byte 1><place>. <name>        <time>  <points>" (one space less before two-digit points) from x 38 with the car's side view at x 113 */
static void league_choices(struct blit_list *l, const struct s2_end_in *in)
{
    char t[24], row[48];
    int n = 0;
    for (int i = 0; i < 4; i++) n += in->cars[i].done != 0;
    s2gfx_sprite_centred(l, s2gfx_banner(in->track), 4);
    const int lit = (E.blink & BLINK_BIT) != 0;
    if (in->replay) {                   /* REPLAY and CONTINUE in halves (0x4849f ..) */
        for (int k = 0; k < 2; k++) {
            const int on = E.choice == k && lit;
            s2gfx_text_boxed(l, on ? SMALL_HOT : SMALL, on ? HOT : WHITE, 2, 160 * k, 160, 184, k ? s2gfx_line("CONTINUE", 0, "CONTINUE") : s2gfx_line("REPLAY", 0, "REPLAY"));
        }
    } else {
        s2gfx_text_centred(l, lit ? SMALL_HOT : SMALL, lit ? HOT : WHITE, 2, 184, s2gfx_line("CONTINUE", 0, "CONTINUE"));
    }
    const int y0 = 81 - 5 * (n - 1);
    s2gfx_text(l, SMALL, WHITE, 2, 38, y0, s2gfx_line("CONTINUE", 1, "   NAME CAR   TIME    PTS"));
    for (int i = 0; i < 4; i++) {
        const struct s2_end_car *c = &in->cars[i];
        if (!c->done || c->place < 1 || c->place > 4) continue;
        const int y = y0 + 14 + 12 * (c->place - 1), points = s2l_points[c->place - 1];
        char *p = row;
        *p++ = 1;
        *p++ = (char)('0' + c->place);
        *p++ = '.';
        *p++ = ' ';
        for (int k = 0; k < 3 && c->name[k]; k++) *p++ = c->name[k];
        for (int k = 0; k < 8; k++) *p++ = ' ';
        s2_time_text(t, c->time);
        for (const char *s = t; *s; s++) *p++ = *s;
        *p++ = ' ';
        if (points < 10) *p++ = ' ';
        p = fmt_udec(p, (uint32_t)points);
        *p = 0;
        s2gfx_text(l, SMALL, WHITE, 2, 38, y, row);
        s2gfx_sprite(l, S2G_SPR_CAR + (c->team - 1) * 5 + (c->car - 1), 113, y - 1);
    }
}

int s2end_frame(volatile uint8_t *fb, int buffer, const uint32_t *pal, const struct s2_end_in *in, uint32_t fin_frames, uint32_t vf,
                uint32_t press)
{
    if (fin_frames == 0 || !E.started) {                /* the player has just finished */
        const int again = in->replaying && E.started;   /* the replay's screen: the race's own left the list where it is (0x47790 does not start it over in a replay) */
        const int rank = E.rank, first = E.first;
        const float frac = E.frac;
        memset(&E, 0, sizeof E);
        E.started = 1;
        E.last_vf = vf;
        E.blink = BLINK_BIT;
        E.rank = -1;
        E.kind = in->league ? S2S_KIND_LEAGUE : in->attack ? S2S_KIND_ATTACK : S2S_KIND_ARCADE;
        if (again) {
            E.recorded = 1;
            E.rank = rank;
            E.first = first;
            E.frac = frac;
        }
        s2gfx_dark_table(pal, E.dark, 256);
    }
    uint32_t dvf = (vf - E.last_vf) & 0xFFFF;
    E.last_vf = vf;
    E.clock += 2 * dvf;
    E.blink += 2 * dvf;
    int result = S2END_NONE;
    struct blit_list *l = s2gfx_begin_on(buffer);
    if (E.clock < CONGRATS_END) {
        congratulations(l, in);
    } else if (in->league) {
        if (!E.recorded) record(in);
        if (in->replay) {
            if ((press & KEY_LEFT) && E.choice > 0) {
                E.choice--;
                E.blink = BLINK_BIT;
            }
            if ((press & KEY_RIGHT) && E.choice < 1) {
                E.choice++;
                E.blink = BLINK_BIT;
            }
        }
        if (press & S2PAGE_ENTER) result = in->replay && E.choice == 0 ? S2END_REPLAY : S2END_CONTINUE;
        int n = 0;
        for (int i = 0; i < 4; i++) n += in->cars[i].done != 0;
        s2gfx_dark_fb(fb, 0, 180, 320, 20, E.dark);
        if (n > 0) s2gfx_dark_fb(fb, 6, 81 - 5 * (n - 1) - 2, 312, 13 * n + 14, E.dark);
        league_choices(l, in);
    } else {
        if (!E.recorded) record(in);
        if ((press & KEY_LEFT) && E.choice > 0) {
            E.choice--;
            E.blink = BLINK_BIT;
        }
        if ((press & KEY_RIGHT) && E.choice < (in->replay ? 2 : 1)) {
            E.choice++;
            E.blink = BLINK_BIT;
        }
        if (press & S2PAGE_ENTER) {
            const int k = in->replay ? E.choice : E.choice + 1;         /* (0x478d3: without a replay the choices count from RACE AGAIN) */
            result = k == 0 ? S2END_REPLAY : k == 1 ? S2END_AGAIN : S2END_EXIT;
        }
        s2gfx_dark_fb(fb, 0, 180, 320, 20, E.dark);     /* the band, then the list's box: 0x51868 works in words, so x 30 is 28 */
        s2gfx_dark_fb(fb, 30, 78, 260, 64, E.dark);
        choices(l, in);
        int target = E.rank < 0 ? 0 : E.rank - 2;       /* the list moves up to bring the player's row to the third place */
        if (target < 0) target = 0;
        if (target >= ROWS) target = 10;
        if (E.first < target && E.clock >= SCROLL_START) {
            E.frac += (float)dvf * 0.08f;               /* four rows a second */
            if (E.frac > 1.0f) {
                E.frac = 0.0f;
                E.first++;
            }
        }
    }
    s2gfx_flush();
    return result;
}
