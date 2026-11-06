/*
 * The attract sequence's screen (s2attract.h). STARTL.EXE 0x37ec1 is the routine the race screen calls in its place in the attract race; the stage numbers and the
 * variables (0x127ecc, 0x127ed0, 0x127ed4, 0x128888, 0x128890, 0x132498, 0x13249c, 0x11f09c) are its own. Every number below is read from its code: the logos' target
 * (0x38069 .. 0x380b4), the ease (0x111d6 with the row 0x55), the 5.0 (0x11119e), 3 s (0x38181), 1.2 (0x1111a6), 0.25 (0x1111ae), 2.0 (0x1111b6), the credits' y (0x382f4),
 * the palette's fade (0x514c1: the 6-bit entries, cut by the program's chop-mode rounding 0x4ebe2).
 */
#include "s2attract.h"
#include "s2gfx.h"
#include "zip.h"
#include "dat.h"
#include "dlog.h"
#include <string.h>

#define EASE        0.925f              /* the table's row 0x55 at 50 frames a second: 0.5 + 0x55 x 0.005 */
#define STEP        0.02f

/* ------------------------------------------------------------------------------------------------ the text */

static char text_buf[2048];

int s2att_text(struct s2att *a, const char *text)
{
    a->n = 0;
    size_t len = strlen(text);
    if (len >= sizeof text_buf) len = sizeof text_buf - 1;
    memcpy(text_buf, text, len);
    text_buf[len] = 0;
    for (char *p = text_buf; *p && a->n < S2ATT_LINES;) {       /* (the loader 0x324a7: a line to its end, the carriage return cut off; an empty line is a line) */
        char *s = p;
        while (*p && *p != '\n') p++;
        char *end = p;
        if (*p) p++;
        if (end > s && end[-1] == '\r') end--;
        *end = 0;
        a->line[a->n++] = s;
    }
    return a->n >= 3;
}

int s2att_load(struct s2att *a, struct zip *z)
{
    a->n = 0;
    const struct zip_entry *e = zip_find(z, "DATA/LOCAL.DAT");
    uint8_t *stage = s2gfx_pic(1);
    if (!e || e->size + ZIP_SLACK > S2G_PIC_BYTES || zip_read(z, e, stage, S2G_PIC_BYTES)) return dlog_str("attract: DATA/LOCAL.DAT cannot be read\n"), 0;
    struct dat_entry d;
    static uint8_t raw[sizeof text_buf];
    if (!dat_find(stage, e->size, "ATTRACT.TXT", &d) || d.size >= sizeof raw) return dlog_str("attract: ATTRACT.TXT is not in LOCAL.DAT\n"), 0;
    const uint32_t n = dat_extract(stage, e->size, &d, raw, sizeof raw - 1);
    if (!n) return dlog_str("attract: ATTRACT.TXT is damaged\n"), 0;
    raw[n] = 0;
    return s2att_text(a, (const char *)raw);
}

/* ------------------------------------------------------------------------------------------------ the screen */

void s2att_begin(struct s2att *a)
{
    a->stage = 0;
    a->group = 2;
    a->x = a->shown = a->gap = a->black = a->lit = 0.0f;
    a->leave = a->over = a->cued = 0;
}

/* the logo's place in the middle (0x33425 .. 0x3809f): ((320 - (x offset + width)) >> 1) - x offset / 2, whole numbers */
static float target_of(int sprite)
{
    int xo = 0, w = 0;
    if (!s2gfx_game_sprite_info(sprite, &xo, &w)) return 0.0f;
    return (float)(((320 - (xo + w)) >> 1) - ((xo - (xo >> 31)) >> 1));
}

/* a float as the program stores it after a sum it makes in a double or in the x87's registers */
static float stored(double v)
{
    return (float)v;
}

void s2att_step(struct s2att *a, int press, struct s2att_view *v)
{
    *v = (struct s2att_view){ .sprite = -1, .caption = -1 };
    if (press && !a->leave) a->leave = 1;                                   /* 0x37f4a: ENTER or ESC, while it is not ending already */
    if (a->stage == 0) {                                                    /* 0x37f84: the screen black, the logos' place far right, the credits from line 2 */
        v->black = 1;
        a->group = 2;
        a->stage = 1;
        a->x = 1000.0f;
        a->gap = 0.0f;
    } else if (a->stage < 3) {                                              /* 0x38005: the logos */
        v->black = 1;
        const int sprite = a->stage + 0x6d;
        const float target = target_of(sprite);
        a->x = stored(((double)a->x - (double)target) * (double)EASE + (double)target);     /* 0x111d6: x = (x - target) x row + target, in the x87's registers */
        v->sprite = sprite;
        v->sx = (int)a->x;
        float d = a->x - target;
        if (d < 0.0f) d = -d;
        if (d < 5.0f) {
            v->caption = a->stage - 1;
            a->shown = stored((double)a->shown + (double)STEP);
            if (a->shown > 3.0f) {
                a->shown = 0.0f;
                a->x = 640.0f;
                a->stage++;
                if (a->stage > 2) a->shown = 1.0f;
            }
        }
    } else if (a->stage == 3) {                                             /* 0x381b8: out of white under the title */
        a->lit = a->shown;
        v->white = a->lit;
        v->title = 1;
        if (!a->cued) {
            a->cued = 1;
            v->music = 1;
        }
        a->shown = stored((double)a->shown - (double)STEP * 1.2);
        if (a->shown < 0.0f) {
            a->shown = 1.0f;
            a->gap = 0.0f;
            a->stage++;
        }
    } else if (a->stage >= 4) {                                             /* 0x3826f: the credits (and, from stage 5 on, nothing: not even the title) */
        v->white = a->lit;                                                  /* (the palette is not set again until the way to black: it is what stage 3 left) */
        if (a->stage == 4) {
            v->title = 1;
            if (a->gap != 0.0f) {                                           /* the second between two groups */
                a->gap = stored((double)a->gap - (double)STEP);
                if (!(0.0f < a->gap)) a->gap = 0.0f;
            } else {
                int k = 0;
                while (a->group + k < a->n && a->line[a->group + k][0] != '#') k++;
                v->first = a->group;
                v->count = k;
                if (k == 0) {                                               /* a group with no line: the end of the file */
                    a->stage++;
                    a->leave = 1;
                } else {
                    a->shown = stored((double)a->shown - (double)STEP * 0.25);
                    if (!(0.0f < a->shown)) {
                        a->group += k + 1;
                        a->shown = 1.0f;
                        a->gap = 1.0f;
                    }
                }
            }
        }
    }
    v->dark = a->black;                                                     /* 0x38381: the palette's way to black so far */
    if (a->leave) {                                                         /* 0x383a3 */
        if (a->stage < 3) {
            a->over = 1;
        } else if (a->stage >= 4) {
            a->black = stored((double)STEP * 2.0 + (double)a->black);
            if (!(a->black < 1.0f)) a->over = 1;
        }
    }
    if (a->over) {                                                          /* 0x358d1: the screen black, then the palette back and the S2 logo at y 40 */
        const int music = v->music;
        *v = (struct s2att_view){ .sprite = -1, .caption = -1, .exit = 1, .music = music };
    }
}

void s2att_draw(struct blit_list *l, const struct s2att *a, const struct s2att_view *v)
{
    if (v->black || v->exit) blit_fill(l, 0, 0, 320, 200, 0);
    if (v->exit) {
        s2gfx_game_sprite_centred(l, S2ATT_SPR_S2, 40);
        return;
    }
    if (v->sprite >= 0) s2gfx_game_sprite(l, v->sprite, v->sx, 10);
    if (v->caption >= 0 && v->caption < a->n) s2gfx_game_text_centred(l, 0, 1, 2, 0xa5, a->line[v->caption]);
    if (v->title) s2gfx_game_sprite_centred(l, S2ATT_SPR_TITLE, 4);
    for (int k = 0; k < v->count; k++) s2gfx_game_text_centred(l, 0, 1, 2, 100 + 16 * k, a->line[v->first + k]);
}

/* 0x514c1: the program's palette is 6 bits an entry; each is (c - t) x k + t cut to a whole number (0x4ebe2, chop mode), k = 1 - level (a float) clamped to 0 .. 1, t the colour's
   6 bits (255 >> 2 or 0). The x87 does it exactly (a 6-bit number times a 24-bit one is 30 bits): so does this, in whole numbers, k as m / 2^s. */
static uint32_t faded(uint32_t c, float level, int t)
{
    float k = 1.0f - level;
    if (0.0f > k) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    uint32_t b;
    memcpy(&b, &k, 4);
    const int e = (int)(b >> 23 & 255);
    const int64_t m = e ? (int64_t)((b & 0x7FFFFFu) | 0x800000u) : 0;     /* k = m / 2^s */
    const int s = 150 - e;
    uint32_t out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        const int v = (int)(c >> sh & 255) >> 2, d = v - t;
        int r;
        if (!m || s > 57) r = t;                                        /* (k of 0, or too small for the x87's 64 bits to see) */
        else r = (int)((((int64_t)t << s) + (int64_t)d * m) >> s);      /* (the sum is not negative: t is 63 with d <= 0, or 0 with d >= 0) */
        out |= (uint32_t)r << 2 << sh;
    }
    return out;
}

int s2att_palette(const uint32_t *pal, const struct s2att_view *v, uint32_t out[256])
{
    if (v->exit || (v->white == 0.0f && v->dark == 0.0f)) return 0;
    for (int i = 0; i < 256; i++) {
        const uint32_t c = pal[i] & 0xFFFFFFu;
        out[i] = v->dark != 0.0f ? faded(c, v->dark, 0) : faded(c, v->white, 63);
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the tracks */

static const uint8_t tracks[6] = { 1, 3, 4, 5, 6, 2 };
static int counter;

int s2att_track(void) { return tracks[counter]; }
void s2att_next_track(void) { counter = (counter + 1) % 6; }
