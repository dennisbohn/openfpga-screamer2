/*
 * The end of the race (s2end.h)
 */
#include "s2end.h"
#include "s2gfx.h"
#include "s2load.h"
#include "s2page.h"
#include "s2save.h"
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
    int choice;                         /* 0 RACE AGAIN, 1 EXIT */
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
    E.rank = s2save_add_time(S2S_KIND_ARCADE, in->track, s2_laps_list(in->laps), in->time, 0, in->team, in->car, 0);
    if (in->best_lap > 0.0f) s2save_add_time(S2S_KIND_ARCADE, in->track, S2S_LAP, in->best_lap, 0, in->team, in->car, 0);
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

static char *put_int(char *p, int v)
{
    if (v >= 10) *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* CONGRATULATIONS ! (first place), "1ST POSITION", the total time: the race screen's font 0, colour 1, spacing 2, centred at y 70, 100, 130 */
static void congratulations(struct blit_list *l, const struct s2_end_in *in)
{
    char t[24];
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
    for (int k = 0; k < 2; k++) {
        int lit = E.choice == k && (E.blink & BLINK_BIT);
        s2gfx_text_boxed(l, lit ? SMALL_HOT : SMALL, lit ? HOT : WHITE, 2, 160 * k, 160, 184, s2gfx_line("RACE AGAIN", k, k ? "EXIT" : "RACE AGAIN"));
    }
    int y0 = (int)(80.0f - 12.0f * E.frac), list = s2_laps_list(in->laps);
    s2gfx_clip(0x51, 0x8C, 0x50, 0x8C);
    for (int i = E.first, y = y0; i < E.first + VIEW && i < ROWS; i++, y += 12) {
        const struct s2save_row *r = s2save_row(S2S_KIND_ARCADE, in->track, list, i);
        char t[40], *p = t;
        *p++ = 1;                                       /* fixed pitch */
        if (i < 9) *p++ = ' ';
        p = put_int(p, i + 1);
        *p++ = '.';
        *p++ = ' ';
        for (int k = 0; k < 4 && r->name[k]; k++) *p++ = r->name[k];
        for (int k = 0; k < 6; k++) *p++ = ' ';
        s2_time_text(p, r->time);
        int mine = i == E.rank;
        s2gfx_text(l, mine ? SMALL_HOT : SMALL, mine ? HOT : WHITE, 2, 50, y, t);
        s2gfx_sprite(l, S2G_SPR_CAR + (r->team - 1) * 5 + (r->car - 1), 134, y - 1);
    }
    s2gfx_clip(0, FB_H, 0, FB_H);
}

int s2end_frame(volatile uint8_t *fb, int buffer, const uint32_t *pal, const struct s2_end_in *in, uint32_t fin_frames, uint32_t vf,
                uint32_t press)
{
    if (fin_frames == 0 || !E.started) {                /* the player has just finished */
        memset(&E, 0, sizeof E);
        E.started = 1;
        E.last_vf = vf;
        E.blink = BLINK_BIT;
        E.rank = -1;
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
    } else {
        if (!E.recorded) record(in);
        if ((press & KEY_LEFT) && E.choice > 0) {
            E.choice--;
            E.blink = BLINK_BIT;
        }
        if ((press & KEY_RIGHT) && E.choice < 1) {
            E.choice++;
            E.blink = BLINK_BIT;
        }
        if (press & S2PAGE_ENTER) result = E.choice ? S2END_EXIT : S2END_AGAIN;
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
