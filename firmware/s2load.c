/*
 * The LOADING screen (s2load.h)
 */
#include "s2load.h"
#include "s2gfx.h"
#include "s2save.h"
#include "console.h"
#include "music.h"
#include "dlog.h"
#include "hw.h"
#include <string.h>

#define FADE_FRAMES 20                  /* 0.4 s */
#define SPR_BANNER  0x13                /* the track name banners: MENUL.SPR 19 .. 25, 144 x 50 */

static uint32_t pal[256];
static int track_shown, laps_shown, kind_shown = S2S_KIND_ARCADE, up;

/* the list of a race of `laps` laps: the NUMBER OF LAPS value's place (1, 3, 5, 10) */
int s2_laps_list(int laps)
{
    return laps >= 10 ? 3 : laps >= 5 ? 2 : laps >= 3 ? 1 : 0;
}

/* The first rows of the lists of a kind of game (the arcade's, the championship's: the end of a league race puts the player's times in its own) */
void s2_best_get_kind(int kind, int track, int laps, struct s2_record *total, struct s2_record *lap)
{
    const struct s2save_row *t = s2save_row(kind, track, s2_laps_list(laps), 0), *l = s2save_row(kind, track, S2S_LAP, 0);
    *total = (struct s2_record){ 3599.999f, "MIL" };
    *lap = (struct s2_record){ 3599.999f, "MIL" };
    if (t) {
        total->time = t->time;
        memcpy(total->name, t->name, 4);
    }
    if (l) {
        lap->time = l->time;
        memcpy(lap->name, l->name, 4);
    }
}

void s2_best_get(int track, int laps, struct s2_record *total, struct s2_record *lap)
{
    s2_best_get_kind(S2S_KIND_ARCADE, track, laps, total, lap);
}

/* minutes, seconds, hundredths as 0x37e1e prints them: the whole seconds cut off, the hundredths of what is left cut off too (the
   product in double: a float times 100 is exact there, as it is in the x87's registers) */
char *s2_time_text(char *p, float t)
{
    if (!(t > 0.0f) || t > 3599.9f) return fmt_str(p, "--:--:--");
    uint32_t whole = (uint32_t)t, hundredths = (uint32_t)((double)(t - (float)whole) * 100.0);
    uint32_t v[3] = { whole / 60 % 100, whole % 60, hundredths };
    for (int k = 0; k < 3; k++) {
        if (k) *p++ = ':';
        *p++ = (char)('0' + v[k] / 10);
        *p++ = (char)('0' + v[k] % 10);
    }
    *p = 0;
    return p;
}

void s2_row_text(char *p, int row, const struct s2save_row *r)
{
    *p++ = 1;
    if (row < 9) *p++ = ' ';
    if (row >= 9) *p++ = (char)('0' + (row + 1) / 10);
    *p++ = (char)('0' + (row + 1) % 10);
    *p++ = '.';
    *p++ = ' ';
    for (int k = 0; k < 4 && r->name[k]; k++) *p++ = r->name[k];
    for (int k = 0; k < 6; k++) *p++ = ' ';
    s2_time_text(p, r->time);
}

void s2_record_text(char *p, const struct s2_record *r)
{
    p = s2_time_text(p, r->time);
    *p++ = ' ';
    for (int k = 0; k < 4 && r->name[k]; k++) *p++ = r->name[k];
    *p = 0;
}

/* the screen into the frame being built: the picture, the banner (y 4), BEST TIME (label y 72, record y 84), BEST LAP (112, 124),
   LOADING... (170); the original's font 1 for the records and the title, 2 for the labels, colour 1, spacing 2 */
static void draw_loading(struct blit_list *l)
{
    char t[24];
    struct s2_record total, lap;
    s2_best_get_kind(kind_shown, track_shown, laps_shown, &total, &lap);
    s2gfx_picture(l, s2gfx_pic(0));
    s2gfx_sprite_centred(l, SPR_BANNER + track_shown - 1, 4);
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 170, s2gfx_line("LOADING...", 0, "LOADING..."));
    if (kind_shown >= S2S_KINDS) return;        /* the free run keeps no records and shows none (0x34fb7: the kinds 3 and 4 skip the lines) */
    s2gfx_text_centred(l, S2G_FONT_SMALL, S2G_WHITE, 2, 72, s2gfx_line("BEST TIME", 0, "BEST TIME"));
    s2_record_text(t, &total);
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 84, t);
    s2gfx_text_centred(l, S2G_FONT_SMALL, S2G_WHITE, 2, 112, s2gfx_line("BEST LAP", 0, "BEST LAP"));
    s2_record_text(t, &lap);
    s2gfx_text_centred(l, S2G_FONT_MEDIUM, S2G_WHITE, 2, 124, t);
}

const uint32_t *s2load_show(int track, int laps)
{
    return s2load_show_kind(S2S_KIND_ARCADE, track, laps);
}

const uint32_t *s2load_show_kind(int kind, int track, int laps)
{
    char name[16];
    uint32_t t0 = rdcycle();
    up = 0;
    if (track < 1 || track > 7) return 0;
    fmt_str(fmt_udec(fmt_str(name, "LOAD_"), (uint32_t)track), "L.BKK");
    if (!s2gfx_bkk(name, s2gfx_pic(0), pal)) {
        dlog_str("ui: ");
        dlog_str(name);
        dlog_str(" not usable\n");
        return 0;
    }
    track_shown = track;
    laps_shown = laps;
    kind_shown = kind;
    struct blit_list *l = s2gfx_begin();
    draw_loading(l);
    s2gfx_end(pal, 0x8000);
    up = 1;
    static int logged;
    if (!logged++) {                    /* (the log has 4 KB: the first one tells how long the picture takes) */
        dlog_str("ui: loading screen up after ");
        dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
        dlog_str(" ms\n");
    }
    return pal;
}

void s2load_fade(void)
{
    if (!up) return;
    for (int32_t f = 0x8000 - 0x8000 / FADE_FRAMES; f > 0; f -= 0x8000 / FADE_FRAMES) {
        struct blit_list *l = s2gfx_begin();
        music_frame();                  /* (the race's song begins here: its first reads are in flight, music.h) */
        draw_loading(l);
        s2gfx_end(pal, f);
    }
    up = 0;
}
