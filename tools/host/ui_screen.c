/*
 * The 2D toolkit on the PC (firmware/s2gfx.c, s2page.c, s2load.c, s2pause.c): the same code that runs on the Pocket, with the blitter's
 * C model for the hardware, a swap that flips two framebuffers and scripted buttons. Called from menu_screen.c (tools/menu_screens.sh)
 * with the references of tools/ui_reference.py: the .BKK pictures against an independent reader (CRC-32 of pixels and palette, on a
 * stored and on a deflated ZIP), text and sprites against an independent renderer of the original's text routine, the loading screens
 * for the seven tracks, the pause menu on real race pictures (keys, sounds, the darkened still), the page runner's fades and key repeat,
 * and the 15 menu samples of SOUND.DAT's MENU.DTR. Pictures go to <prefix>_<name>.rgb. Returns the number of failures.
 */
#define _GNU_SOURCE
#include "../s2gfx.h"
#include "../s2page.h"
#include "../s2load.h"
#include "../s2pause.h"
#include "../s2arcade.h"
#include "../s2menu.h"
#include "../s2save.h"
#include "../s2snd.h"
#include "../s2pages.h"
#include "../s2end.h"
#include "../s2save.h"
#include "../s2menu.h"
#include "../frame.h"
#include "../hw.h"
#include "../blit.h"
#include "../crc32.h"
#include "../dat.h"
#include "../dlog.h"
#include "../screen.h"
#include "../zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------------------------------------ the hardware's stand-ins */

static int front;                       /* the buffer on show */
static uint32_t calls;                  /* wait_swap_done() calls so far: the video frame counter */
static const char *script;              /* the buttons of each call: one character a frame, ' ' or '.' none, U D L R A B S(tart) s(elect) X, < > the shoulders */
static uint32_t script_at;
static const char *shot_prefix;
static struct { uint32_t at; const char *name; } shots[16];
static int nshots;
static uint32_t pal_trace[128][2];       /* for the fade tests: palette entries 17 and 241 as each frame left them */
static uint32_t swaps;
/* frames to compare with a reference when they are on show: the page's frame of tick t is on show in call t + 1 */
struct expect { uint32_t at; char name[96]; int shot; };
static struct expect expects[40];
static int nexpects, expect_hits;
static const char *expect_ref;
static uint8_t *slurp(const char *path, uint32_t *n);
static void check_frame(const struct expect *e);
static void save(const char *name);
static struct { uint32_t at; int slot; } caps[12];      /* frames kept in memory: the picture on show in a call, for the comparisons */
static int ncaps;
static uint8_t cap[12][S2G_PIC_BYTES];

/* hw.h's registers are plain memory here: the swap request is a store to VID_SWAP */
static void save(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s_%s.rgb", shot_prefix, name);
    FILE *o = fopen(path, "wb");
    const volatile uint8_t *fb = FB(front);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t col = VID_PAL[fb[i]];
        uint8_t rgb[3] = { (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

uint32_t wait_swap_done(void)
{
    if (VID_SWAP) {                     /* the frame drawn since the last call goes on show */
        VID_SWAP = 0;
        front ^= 1;
        if (swaps < 128) {
            pal_trace[swaps][0] = VID_PAL[17];
            pal_trace[swaps][1] = VID_PAL[241];
        }
        swaps++;
        for (int i = 0; i < nshots; i++)
            if (shots[i].at == calls) save(shots[i].name);
        for (int i = 0; i < nexpects; i++)
            if (expects[i].at == calls) check_frame(&expects[i]);
        for (int i = 0; i < ncaps; i++)
            if (caps[i].at == calls) memcpy(cap[caps[i].slot], (const void *)FB(front), S2G_PIC_BYTES);
    }
    uint32_t keys = 0;
    char c = script && script[script_at] ? script[script_at++] : '.';
    switch (c) {
    case 'U': keys = KEY_UP; break;
    case 'D': keys = KEY_DOWN; break;
    case 'L': keys = KEY_LEFT; break;
    case 'R': keys = KEY_RIGHT; break;
    case 'A': keys = KEY_A; break;
    case 'B': keys = KEY_B; break;
    case 'S': keys = KEY_START; break;
    case 's': keys = KEY_SELECT; break;
    case 'X': keys = KEY_X; break;
    case '<': keys = KEY_L; break;
    case '>': keys = KEY_R; break;
    default: break;
    }
    IN_KEY = keys;
    return (uint32_t)front | calls++ << 16;
}

void hw_blit(const struct blit_list *l)
{
    blit_model(FB(l->buffer), l);
}

void pal_faded(const uint32_t *pal, int32_t f)         /* main.c's */
{
    uint32_t k = f < 0 ? 0 : f > 0x8000 ? 0x8000 : (uint32_t)f;
    for (int i = 0; i < 256; i++) {
        uint32_t c = pal[i];
        VID_PAL[i] = k == 0x8000 ? (c & 0xFFFFFFu) : ((c >> 16 & 255) * k >> 15) << 16 | ((c >> 8 & 255) * k >> 15) << 8 | (c & 255) * k >> 15;
    }
}

/* the music and the effects: what the pause asks of them */
static int music_frames, effects_calls, clicks[64], nclicks;
static int32_t effects_last, effects_min = 0x8000;
void music_frame(void) { music_frames++; }
void s2sound_effects(int32_t f)
{
    effects_calls++;
    effects_last = f;
    if (f < effects_min) effects_min = f;
}
void s2sound_menu(int id)
{
    if (nclicks < 64) clicks[nclicks++] = id;
}

static void begin_run(const char *keys, const char *prefix)
{
    script = keys;
    script_at = 0;
    IN_KEY = 0;
    calls = 0;
    swaps = 0;
    nshots = ncaps = 0;
    shot_prefix = prefix;
    music_frames = effects_calls = nclicks = 0;
    effects_min = 0x8000;
    nexpects = 0;
}

static void shot_at(uint32_t call, const char *name)
{
    shots[nshots].at = call;
    shots[nshots++].name = name;
}

static void cap_at(uint32_t call, int slot)
{
    caps[ncaps].at = call;
    caps[ncaps++].slot = slot;
}

/* ------------------------------------------------------------------------------------------------ helpers */

static uint8_t *slurp(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    *n = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n + 8);
    if (fread(b, 1, *n, f) != *n) { free(b); b = 0; }
    fclose(f);
    return b;
}

/* the pictures: every 320 x 200 .BKK of GRAF1.DAT against the reader of tools/ui_reference.py */
static void check_pictures(const char *ref, const char *what)
{
    char path[512];
    snprintf(path, sizeof path, "%s/expect.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "%s: no expect.txt", ref);
    int n = 0;
    char name[64], kind[8];
    unsigned px, pl;
    static uint32_t pal[256];
    while (f && fscanf(f, "%7s", kind) == 1) {
        if (!strcmp(kind, "bkk") && fscanf(f, "%63s %x %x", name, &px, &pl) == 3) {
            int ok = s2gfx_bkk(name, s2gfx_pic(0), pal);
            CHECK(ok, "%s: %s not read", what, name);
            if (ok) {
                uint32_t cp = crc32(s2gfx_pic(0), s2gfx_pic(0) + S2G_PIC_BYTES), cl = crc32((const uint8_t *)pal, (const uint8_t *)(pal + 256));
                CHECK(cp == px, "%s: %s pixels crc %08x, expected %08x", what, name, cp, px);
                CHECK(cl == pl, "%s: %s palette crc %08x, expected %08x", what, name, cl, pl);
                n++;
            }
        } else {
            char rest[128];
            if (!fgets(rest, sizeof rest, f)) break;
        }
    }
    if (f) fclose(f);
    CHECK(n == 28, "%s: %d pictures checked, 28 expected", what, n);
    CHECK(!s2gfx_bkk("NOSUCH_L.BKK", s2gfx_pic(0), pal), "%s: a missing picture reads as one", what);
    printf("ui: %s: %d .BKK pictures equal to the independent reader\n", what, n);
}

/* the text and sprite cases against the independent renderer */
static void check_text(const char *ref, const char *prefix)
{
    char path[512], line[256];
    snprintf(path, sizeof path, "%s/cases.txt", ref);
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    uint8_t *want;
    snprintf(path, sizeof path, "%s/cases.idx", ref);
    want = slurp(path, &n);
    CHECK(f && want && n == S2G_PIC_BYTES, "text cases or references missing");
    if (!f || !want) return;
    static struct blit_list l;
    blit_start(&l, 0);
    volatile uint8_t *fb = FB(0);
    memset((void *)fb, 0, S2G_PIC_BYTES);
    int cases = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        int a, b, c, d, e, off = 0;
        if (sscanf(line, "text %d %d %d %d %d %n", &a, &b, &c, &d, &e, &off) >= 5 && off) {
            s2gfx_text(&l, a, b, c, d, e, line + off);
        } else if (sscanf(line, "ctext %d %d %d %d %n", &a, &b, &c, &d, &off) >= 4 && off) {
            s2gfx_text_centred(&l, a, b, c, d, line + off);
        } else if (sscanf(line, "spr %d %d %d", &a, &b, &c) == 3) {
            s2gfx_sprite(&l, a, b, c);
        } else if (sscanf(line, "cen %d %d", &a, &b) == 2) {
            s2gfx_sprite_centred(&l, a, b);
        } else {
            CHECK(0, "case not understood: %s", line);
            continue;
        }
        cases++;
    }
    fclose(f);
    CHECK(l.n < BLIT_MAX, "text cases overflow the blitter list (%d)", l.n);
    blit_model(fb, &l);
    int differ = 0, first = -1;
    for (int i = 0; i < S2G_PIC_BYTES; i++)
        if (fb[i] != want[i] && differ++ == 0) first = i;
    CHECK(!differ, "text and sprites: %d pixels differ from the independent renderer, first at x %d y %d (%02x, expected %02x)", differ,
          first % 320, first / 320, first >= 0 ? fb[first] : 0, first >= 0 ? want[first] : 0);
    printf("ui: %d text and sprite cases (%d blitter commands) equal to the independent renderer\n", cases, l.n);
    /* the width of a string is what the drawing advances */
    CHECK(s2gfx_text(0, S2G_FONT_MEDIUM, 1, 2, 0, 0, "") == 0, "empty string has a width");
    int w1 = s2gfx_text(0, S2G_FONT_MEDIUM, 1, 2, 0, 0, "RESUME"), w2 = s2gfx_text(0, S2G_FONT_MEDIUM, 1, 3, 0, 0, "RESUME");
    CHECK(w2 == w1 + 5, "spacing 3 instead of 2 widens six characters by five pixels, one a gap (%d, %d)", w1, w2);
    static uint32_t pal[256];                                       /* the picture to look at: the cases over a blue grid's palette */
    if (s2gfx_bkk("BKG_2L.BKK", s2gfx_pic(1), pal)) {
        for (int i = 0; i < 256; i++) VID_PAL[i] = pal[i];
        front = 0;
        shot_prefix = prefix;
        save("text");
    }
    free(want);
}

/* ------------------------------------------------------------------------------------------------ the loading screens */

static void check_loading(const char *prefix)
{
    struct s2_record none = { 3599.999f, "MIL" }, r = { 83.456f, "ABC" }, big = { 3700.0f, "XYZ" }, zero = { 0.0f, "ZZZ" };
    char t[24];
    s2_record_text(t, &none);
    CHECK(!strcmp(t, "--:--:-- MIL"), "empty record reads '%s'", t);
    s2_record_text(t, &r);
    CHECK(!strcmp(t, "01:23:45 ABC"), "83.456 s reads '%s'", t);
    s2_record_text(t, &big);
    CHECK(!strcmp(t, "--:--:-- XYZ"), "3700 s reads '%s'", t);
    s2_record_text(t, &zero);
    CHECK(!strcmp(t, "--:--:-- ZZZ"), "0 s reads '%s'", t);
    CHECK(s2load_show(0, 3) == 0 && s2load_show(8, 3) == 0, "a track out of range shows a picture");
    for (int track = 1; track <= 7; track++) {
        char name[16], what[24];
        snprintf(name, sizeof name, "load%d", track);
        begin_run("", prefix);
        const uint32_t *pal = s2load_show(track, 3);
        CHECK(pal != 0, "loading screen of track %d", track);
        if (!pal) continue;
        wait_swap_done();                                           /* the swap goes in */
        if (track == 1 || track == 4 || track == 7) save(name);
        /* on show: the picture with its own palette, whole */
        const volatile uint8_t *fb = FB(front);
        int dark = 0;
        for (int i = 0; i < 320 * 8; i++) dark += fb[i] == 0;
        CHECK(VID_PAL[17] == pal[17], "track %d: the palette is not the picture's", track);
        snprintf(what, sizeof what, "banner %d", track);
        int banner = 0;                                             /* the banner sprite's rows (y 4 .. 53) have pixels of their own */
        for (int y = 4; y < 54; y++)
            for (int x = 88; x < 232; x++) banner += fb[y * 320 + x] != 0;
        CHECK(banner > 500, "track %d: %s is not drawn (%d)", track, what, banner);
        begin_run("", prefix);
        s2load_fade();
        CHECK(swaps == 19 || swaps == 20, "track %d: the fade took %u frames", track, swaps);
        CHECK(pal_trace[0][0] > pal_trace[swaps - 1][0] || pal_trace[0][0] == 0, "track %d: the fade does not darken", track);
        CHECK(VID_PAL[17] < pal[17] / 4, "track %d: the fade ends bright (%06x)", track, VID_PAL[17]);
    }
    printf("ui: loading screens of the seven tracks drawn, the record text and the fade checked\n");
}

/* ------------------------------------------------------------------------------------------------ the pause menu */

static void check_pause(const char *ref, const char *prefix)
{
    static const char *const frames[3] = { "s2_track4_city", "s2_track1_village", "s2_track5_snow" };
    static uint32_t pal[256], seen[256];
    static uint8_t dark[256];
    for (int k = 0; k < 3; k++) {
        char path[512];
        uint32_t n, np;
        snprintf(path, sizeof path, "%s/frame_%s.idx", ref, frames[k]);
        uint8_t *idx = slurp(path, &n);
        snprintf(path, sizeof path, "%s/frame_%s.pal", ref, frames[k]);
        uint8_t *pb = slurp(path, &np);
        CHECK(idx && pb && n == S2G_PIC_BYTES && np == 1024, "%s: reference frame missing", frames[k]);
        if (!idx || !pb) continue;
        memcpy(pal, pb, 1024);
        memcpy((void *)FB(0), idx, S2G_PIC_BYTES);
        front = 0;
        for (int i = 0; i < 256; i++) VID_PAL[i] = pal[i];
        /* DOWN, DOWN, UP (RESTART chosen), then B: RESTART. The frames to look at: the first, after a move, the blink's off phase */
        char tag[8][32];
        for (int i = 0; i < 8; i++) snprintf(tag[i], sizeof tag[i], "pause%d_%d", k + 1, i);
        begin_run("....D...D...U....B", prefix);
        shot_at(3, tag[0]);                                         /* RESUME bright (the swap of tick 2 is seen in call 3) */
        shot_at(8, tag[1]);                                         /* RESTART */
        shot_at(12, tag[2]);                                        /* EXIT */
        shot_at(17, tag[3]);                                        /* RESTART again, in the blink's off half: dim like the others */
        int r = s2pause_run(front, pal);
        CHECK(r == S2PAUSE_RESTART, "%s: DOWN DOWN UP B chose %d, not RESTART", frames[k], r);
        CHECK(nclicks == 4 && clicks[0] == S2PAGE_SND_MOVE && clicks[1] == S2PAGE_SND_MOVE && clicks[2] == S2PAGE_SND_MOVE &&
              clicks[3] == S2PAGE_SND_SELECT, "%s: clicks: %d", frames[k], nclicks);
        CHECK(music_frames == (int)calls, "%s: the music ring was kept fed %d times in %u frames", frames[k], music_frames, calls);
        CHECK(effects_last == 0x8000 && effects_min < 0x8000 && effects_min > 0x8000 - 18 * 700, "%s: the effects ease to %d, end at %d", frames[k], effects_min, effects_last);

        /* the still: the picture on show through the darkening table, the box under the items twice */
        s2gfx_dark_table(pal, dark, 241);
        const uint8_t *still = s2gfx_pic(1);
        int bad = 0, high = 0;
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                uint8_t want = dark[idx[y * 320 + x]];
                if (x >= 0x50 && x < 0xF0 && y >= 0x5E - 33 && y < 0x5E - 33 + 78) want = dark[want];
                bad += still[y * 320 + x] != want;
                high += still[y * 320 + x] >= 241;
            }
        CHECK(!bad, "%s: %d pixels of the still are not the picture through the table", frames[k], bad);
        CHECK(!high, "%s: %d pixels of the still use the dim ramp", frames[k], high);
        /* the table: the entry nearest to 0.67 of each colour in double precision, or, where the colour is itself that entry, the one
           nearest to 0.7 of that (a tolerance for the float arithmetic of the firmware) */
        int wrong = 0;
        for (int i = 0; i < 256; i++) {
            double best[2] = { 1e30, 1e30 }, got[2] = { 0, 0 }, self = 0;
            for (int pass = 0; pass < 2; pass++)
                for (int q = 0; q < 241; q++) {
                    double d = 0, k2 = pass ? 0.469 : 0.67;
                    for (int sh = 16; sh >= 0; sh -= 8) {
                        double e = (double)(pal[q] >> sh & 255) - (double)(pal[i] >> sh & 255) * k2;
                        d += e * e;
                    }
                    if (d < best[pass]) best[pass] = d;
                    if (q == dark[i]) got[pass] = d;
                    if (!pass && q == i) self = d;
                }
            int nearest0 = got[0] <= best[0] * 1.0001 + 1e-3;
            int self_nearest = i < 241 && self <= best[0] * 1.0001 + 1e-3;      /* (the first of equals could be another entry) */
            int nearest1 = got[1] <= best[1] * 1.0001 + 1e-3;
            wrong += !nearest0 && !(self_nearest && nearest1);
        }
        CHECK(!wrong, "%s: %d colours darkened to an entry that is not the nearest", frames[k], wrong);
        /* the palette on show in the last frame: the dim ramp is the white one at 0.45, the rest is the race's */
        int same = 1;
        for (int i = 0; i < 241; i++) same &= (VID_PAL[i] & 0xFFFFFF) == (pal[i] & 0xFFFFFF);
        CHECK(same, "%s: the race's colours changed in the pause palette", frames[k]);
        CHECK((VID_PAL[241 + 14] >> 16 & 255) == (pal[16 + 15] >> 16 & 255) * 45 / 100, "%s: dim ramp end %06x", frames[k], VID_PAL[255]);
        free(idx);
        free(pb);
        memcpy(seen, pal, sizeof seen);
    }
    /* the other ways out and the cursor's wrap: A resumes at once, UP from RESUME wraps to EXIT, a held DOWN repeats */
    begin_run("..A", prefix);
    CHECK(s2pause_run(front, seen) == S2PAUSE_RESUME, "A resumes");
    begin_run("..U..B", prefix);
    CHECK(s2pause_run(front, seen) == S2PAUSE_EXIT, "UP from RESUME wraps to EXIT");
    begin_run("..S", prefix);
    CHECK(s2pause_run(front, seen) == S2PAUSE_RESUME, "Start on RESUME resumes");
    begin_run("..s", prefix);
    CHECK(s2pause_run(front, seen) == S2PAUSE_RESUME, "Select resumes");
    /* DOWN held for 75 frames: presses at 0 and every 6th from the 25th (10 in all): the cursor ends on RESTART; after 50 frames the effects are off */
    char held[128];
    memset(held, '.', sizeof held);
    memset(held + 3, 'D', 75);
    strcpy(held + 78, ".B");
    begin_run(held, prefix);
    int r = s2pause_run(front, seen);
    CHECK(r == S2PAUSE_RESTART, "a held DOWN chose %d (RESTART expected after 10 presses)", r);
    CHECK(effects_min == 0 && effects_last == 0x8000, "the effects ease to %d and are given back at %d", effects_min, effects_last);
    printf("ui: pause menu: keys, clicks, the darkened still and the easing of the effects checked on three race pictures\n");
}

/* ------------------------------------------------------------------------------------------------ the page runner */

struct demo { int presses, ticks, down; };

static void demo_frame(void *self, uint32_t tick) { ((struct demo *)self)->ticks = (int)tick; }
static int demo_key(void *self, uint32_t press)
{
    struct demo *d = self;
    if (press & KEY_DOWN) d->presses++;
    return (press & KEY_B) ? 7 : 0;
}

static void check_runner(void)
{
    static uint32_t pal[256];
    CHECK(s2gfx_bkk("BKG_1L.BKK", s2gfx_pic(0), pal), "BKG_1L for the runner");
    struct demo d = { 0, 0, 0 };
    char keys[200];
    memset(keys, 'D', 60);                                          /* held for 60 frames: a press at 0, 25, 31, 37, 43, 49, 55 */
    strcpy(keys + 60, "..B");
    const struct s2_page page = { s2gfx_pic(0), pal, 10, 5, demo_frame, demo_key, 0 };
    begin_run(keys, "unused");
    int r = s2page_run(&page, &d);
    CHECK(r == 7, "the page's result is %d", r);
    CHECK(d.presses == 7, "a held DOWN gave %d presses (7 expected: 0, 25, 31, 37, 43, 49, 55)", d.presses);
    CHECK(pal_trace[0][0] == 0, "the first frame is not black (%06x)", pal_trace[0][0]);
    CHECK(pal_trace[9][0] > 0 && pal_trace[9][0] <= pal[17] && pal_trace[10][0] == (pal[17] & 0xFFFFFF) && pal_trace[40][0] == (pal[17] & 0xFFFFFF),
          "fade in: %06x %06x %06x", pal_trace[9][0], pal_trace[10][0], pal_trace[40][0]);
    int leaving = 0;
    for (uint32_t i = 62; i < swaps && i < 128; i++) leaving += pal_trace[i][0] < pal_trace[i - 1][0];
    CHECK(leaving >= 3, "the fade out darkens in %d of the last frames (%u frames; %06x %06x %06x %06x %06x %06x)", leaving, swaps,
          pal_trace[swaps - 6][0], pal_trace[swaps - 5][0], pal_trace[swaps - 4][0], pal_trace[swaps - 3][0], pal_trace[swaps - 2][0], pal_trace[swaps - 1][0]);
    CHECK(swaps >= 65 && swaps <= 70, "the page ran %u frames", swaps);
    /* no fade: a cut, the result at once */
    const struct s2_page cut = { 0, pal, 0, 0, 0, demo_key, 0 };
    begin_run("..B", "unused");
    CHECK(s2page_run(&cut, &d) == 7 && swaps <= 3, "a page with no fade ends with its key (%u swaps)", swaps);
    printf("ui: page runner: key repeat, fade in and out and the cut checked\n");
}

/* ------------------------------------------------------------------------------------------------ the arcade pages */

/* the frame that is on show against the reference of tools/ui_arcade_ref.py: every pixel and the whole palette */
static void check_frame(const struct expect *e)
{
    char path[512];
    uint32_t n, np;
    snprintf(path, sizeof path, "%s/arc_%s.idx", expect_ref, e->name);
    uint8_t *idx = slurp(path, &n);
    snprintf(path, sizeof path, "%s/arc_%s.pal", expect_ref, e->name);
    uint8_t *pal = slurp(path, &np);
    expect_hits++;
    if (!idx || !pal || n != S2G_PIC_BYTES || np != 1024) {
        CHECK(0, "%s: reference frame missing", e->name);
    } else {
        const volatile uint8_t *fb = FB(front);
        int differ = 0, first = -1;
        for (int i = 0; i < S2G_PIC_BYTES; i++)
            if (fb[i] != idx[i] && differ++ == 0) first = i;
        CHECK(!differ, "%s: %d pixels differ from the model, first at x %d y %d (%02x, expected %02x)", e->name, differ, first % 320, first / 320,
              first >= 0 ? fb[first] : 0, first >= 0 ? idx[first] : 0);
        int bad = 0;
        for (int i = 0; i < 256; i++) bad += (VID_PAL[i] & 0xFFFFFF) != ((const uint32_t *)pal)[i];
        CHECK(!bad, "%s: %d palette entries differ from the model", e->name, bad);
        if (e->shot) save(e->name);
    }
    free(idx);
    free(pal);
}

/* a page with the save's state set up as the case says */
static void arcade_save(int unlock, int nopen, int car5)
{
    s2save_reset();
    s2save_unlock_all = unlock;
    if (!unlock) s2save_set(S2S_TRACKS_OPEN, nopen - 3);
    for (int t = 1; t <= 4; t++) s2save_set_car5(t, car5);
}

static void check_arcade_cases(const char *ref, const char *prefix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/arcade.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no arcade.txt");
    static char line[2048];
    int runs = 0, frames = 0;
    while (f && fgets(line, sizeof line, f)) {
        char name[64], script[512], click_text[128], frame_text[1024];
        int page, track, team, car, manual, unlock, nopen, car5, res, ftrack, fteam, fcar, fmanual, calls_want;
        if (sscanf(line, "case %63s %d %d %d %d %d %d %d %d %511s %d %d %d %d %d %d %127s %1023s", name, &page, &track, &team, &car, &manual, &unlock,
                   &nopen, &car5, script, &res, &ftrack, &fteam, &fcar, &fmanual, &calls_want, click_text, frame_text) != 18) {
            CHECK(0, "arcade.txt: line not understood: %s", line);
            continue;
        }
        arcade_save(unlock, nopen, car5);
        struct s2_menu_cfg c = { .track = track, .team = team, .car = car, .laps = 1, .skill = 1, .manual = manual };
        begin_run(script, prefix);
        expect_ref = ref;
        expect_hits = 0;
        if (strcmp(frame_text, "-"))
            for (char *p = frame_text; *p && nexpects < 40;) {
                int tick = atoi(p);
                char *end = strchr(p, ',');
                expects[nexpects].at = (uint32_t)tick + 1;
                snprintf(expects[nexpects].name, sizeof expects[nexpects].name, "%s_%d", name, tick);
                expects[nexpects].shot = strchr(p, '*') && (!end || strchr(p, '*') < end);
                nexpects++;
                p = end ? end + 1 : p + strlen(p);
            }
        int want_frames = nexpects;
        front = 0;
        int r = s2arcade_page(page, &c);
        CHECK(r == res, "%s: the page ended with %d, expected %d", name, r, res);
        CHECK(c.track == ftrack && c.team == fteam && c.car == fcar && c.manual == fmanual, "%s: ends with track %d team %d car %d gear %d, expected %d %d %d %d",
              name, c.track, c.team, c.car, c.manual, ftrack, fteam, fcar, fmanual);
        CHECK((int)calls == calls_want, "%s: the page took %u frames (calls), expected %d", name, calls, calls_want);
        CHECK(expect_hits == want_frames, "%s: %d of %d frames were compared", name, expect_hits, want_frames);
        char got[128] = "";
        for (int i = 0; i < nclicks; i++) snprintf(got + strlen(got), sizeof got - strlen(got), "%s%d", i ? "," : "", clicks[i]);
        CHECK(!strcmp(nclicks ? got : "-", click_text), "%s: sounds %s, expected %s", name, nclicks ? got : "-", click_text);
        runs++;
        frames += want_frames;
    }
    if (f) fclose(f);
    s2save_reset();
    printf("ui: arcade pages: %d scripted runs, %d frames equal to the model pixel for pixel (title, track page, car page, transmission page)\n", runs, frames);
}

/* the chain: the pages one after the other from one script. A page that ends with ENTER fades out over 25 frames (so the next page starts
   25 frames after the press), one that ends with ESC or the car page's ENTER is a cut (the next starts the frame after) */
static void check_arcade_chain(const char *prefix)
{
    static char s[400];
    s2save_reset();
    struct s2_menu_cfg c = { .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1, .manual = 0 }, rd;
    /* forward: title ENTER, TRACK: R shoulder (the fourth place, FINLAND) and ENTER, CAR: DOWN (team 2) RIGHT (car 2) ENTER, TRANSMISSION: RIGHT
       (manual) ENTER */
    memset(s, '.', sizeof s - 1);
    s[5] = 'B'; s[35] = '>'; s[45] = 'S'; s[75] = 'D'; s[80] = 'R'; s[85] = 'B'; s[90] = 'R'; s[95] = 'B';
    begin_run(s, prefix);
    int r = s2arcade_run(&c);
    CHECK(r == S2ARC_NEXT, "the chain ends with %d (the race)", r);
    CHECK(c.track == 5 && c.team == 2 && c.car == 2 && c.manual == 1, "the chain left track %d team %d car %d gear %d", c.track, c.team, c.car, c.manual);
    CHECK(calls == 120, "the chain took %u frames, expected 120", calls);
    static const int want[] = { 1, 0, 2, 4, 4, 0, 3 };
    CHECK(nclicks == 7 && !memcmp(clicks, want, sizeof want), "the chain's sounds: %d, first %d", nclicks, nclicks ? clicks[0] : -1);
    /* kept: the choices go into the save and come back */
    s2menu_store(&c);
    s2menu_load(&rd);
    CHECK(rd.track == 5 && rd.team == 2 && rd.car == 2 && rd.manual == 1 && rd.laps == 1 && rd.skill == 1, "the chain's choices are kept: %d %d %d %d", rd.track, rd.team, rd.car, rd.manual);
    CHECK(s2save_get(S2S_TRACK) == 5 && s2save_get(S2S_TRANS) == 1, "the save holds the track and the gearbox");

    /* the way back: TRACK ENTER, CAR ESC (back to TRACK), TRACK ESC (out of the chain, to the start menu, which is the original's options room) */
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1, .manual = 0 };
    memset(s, '.', sizeof s - 1);
    s[5] = 'B'; s[35] = 'B'; s[65] = 'A'; s[70] = 'A';
    begin_run(s, prefix);
    r = s2arcade_run(&c);
    CHECK(r == S2ARC_BACK && calls == 71, "ESC out of the TRACK page: result %d after %u frames (expected -1, 71)", r, calls);
    static const int back[] = { 1, 2 };
    CHECK(nclicks == 2 && !memcmp(clicks, back, sizeof back), "back: %d sounds", nclicks);
    /* the transmission page's ESC goes to the CAR page, its ESC to the TRACK page, its ESC out */
    memset(s, '.', sizeof s - 1);
    s[5] = 'B'; s[35] = 'B'; s[65] = 'B'; s[70] = 'A'; s[75] = 'A'; s[80] = 'A';
    begin_run(s, prefix);
    r = s2arcade_run(&c);
    CHECK(r == S2ARC_BACK && calls == 81, "ESC along the chain: result %d after %u frames (expected -1, 81)", r, calls);
    /* ESC on the title leaves at once */
    memset(s, '.', sizeof s - 1);
    s[3] = 'A';
    begin_run(s, prefix);
    CHECK(s2arcade_run(&c) == S2ARC_BACK && calls == 4 && nclicks == 0, "ESC on the title: %u frames, %d sounds", calls, nclicks);
    /* select and start do what A and B do */
    memset(s, '.', sizeof s - 1);
    s[2] = 'S'; s[30] = 's';
    begin_run(s, prefix);
    CHECK(s2arcade_run(&c) == S2ARC_BACK && calls == 31, "Start goes on, Select goes back: %u frames", calls);
    printf("ui: arcade chain: forward, ESC back along it, the sounds, the kept choices checked\n");
}

/* every track is reachable from every other with at most two presses of left / right / L / R, for every number of open tracks (3 to 7) */
static void check_arcade_reach(const char *prefix)
{
    static char s[64];
    int worst_all = 0;
    for (int nopen = 3; nopen <= 7; nopen++) {
        int worst = 0;
        for (int from = 0; from < nopen; from++) {
            int dist[7], prev[7], how[7], queue[7], head = 0, tail = 0;
            for (int i = 0; i < 7; i++) dist[i] = -1;
            dist[from] = 0;
            queue[tail++] = from;
            while (head < tail) {                                       /* a search over the places: LEFT, RIGHT (not round), L and R (round) */
                int p = queue[head++];
                for (int k = 0; k < 4; k++) {
                    int q = k == 0 ? p - 1 : k == 1 ? p + 1 : k == 2 ? ((p - 3) % nopen + nopen) % nopen : (p + 3) % nopen;
                    if (q < 0 || q >= nopen || dist[q] >= 0) continue;
                    dist[q] = dist[p] + 1;
                    prev[q] = p;
                    how[q] = k;
                    queue[tail++] = q;
                }
            }
            for (int to = 0; to < nopen; to++) {
                CHECK(dist[to] >= 0, "place %d cannot be reached from %d with %d open", to, from, nopen);
                if (dist[to] > worst) worst = dist[to];
                int moves[8], n = 0;
                for (int q = to; q != from; q = prev[q]) moves[n++] = how[q];
                memset(s, '.', sizeof s - 1);
                int at = 2;
                while (n) {
                    static const char key[4] = { 'L', 'R', '<', '>' };
                    s[at] = key[moves[--n]];
                    at += 4;
                }
                s[at + 4] = 'B';
                arcade_save(0, nopen, 0);
                struct s2_menu_cfg c = { .track = s2save_track_at(from), .team = 1, .car = 1, .laps = 1, .skill = 1 };
                begin_run(s, prefix);
                front = 0;
                CHECK(s2arcade_page(S2ARC_TRACK, &c) == S2ARC_NEXT && c.track == s2save_track_at(to),
                      "%d open: from place %d the script %.12s gave track %d, expected %d", nopen, from, s, c.track, s2save_track_at(to));
            }
        }
        if (worst > worst_all) worst_all = worst;
        CHECK(worst <= (nopen == 7 ? 2 : 3), "%d open tracks: some track is %d presses away", nopen, worst);
    }
    s2save_reset();
    printf("ui: track page: every track reachable from every other (3 to 7 open) by scripted presses, at most %d each\n", worst_all);
}

/* ------------------------------------------------------------------------------------------------ the menu sounds */

static void check_sounds(struct zip *z, const char *ref)
{
    const struct zip_entry *e = zip_find(z, "DATA/SOUND.DAT");
    uint8_t *buf = (uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000);
    CHECK(e && !zip_read(z, e, buf, 0x200000), "SOUND.DAT from the ZIP");
    if (!e) return;
    static struct s2snd_sample menu[S2SND_MENU_SAMPLES], game[S2SND_SAMPLES];
    int m = s2snd_parse_menu(buf, e->size, 0, menu), g = s2snd_parse(buf, e->size, 0, game);
    CHECK(m == 15, "%d menu samples found, 15 expected", m);
    CHECK(g == 33, "the race's table still gives %d samples (33 expected)", g);
    char path[512], kind[8];
    snprintf(path, sizeof path, "%s/expect.txt", ref);
    FILE *f = fopen(path, "r");
    int n = 0, id;
    unsigned len, rate;
    while (f && fscanf(f, "%7s", kind) == 1) {
        if (!strcmp(kind, "snd") && fscanf(f, "%d %u %u", &id, &len, &rate) == 3 && id >= 0 && id < S2SND_MENU_SAMPLES) {
            CHECK(menu[id].len == len && menu[id].rate == rate && menu[id].vol == 0x7FFF && !menu[id].loop,
                  "menu sample %d: %u bytes at %u Hz, expected %u at %u", id, menu[id].len, menu[id].rate, len, rate);
            n++;
        } else {
            char rest[128];
            if (!fgets(rest, sizeof rest, f)) break;
        }
    }
    if (f) fclose(f);
    CHECK(n == 15, "%d menu samples in the reference", n);
    printf("ui: MENU.DTR: %d menu samples equal to the independent reading (the race's GAME.DTR still gives %d)\n", m, g);
}

/* ------------------------------------------------------------------------------------------------ the pages */

/* the darkening table against a search in double precision: the entry nearest to 0.67 of each colour, or where that is the colour itself,
   the one nearest to 0.7 of that (a tolerance for the float arithmetic of the firmware); the number of colours darkened to something else */
static int table_wrong(const uint32_t *pal, const uint8_t *dark, int keep)
{
    int wrong = 0;
    for (int i = 0; i < 256; i++) {
        double best[2] = { 1e30, 1e30 }, got[2] = { 0, 0 }, self = 0;
        for (int pass = 0; pass < 2; pass++)
            for (int q = 0; q < keep; q++) {
                double d = 0, k2 = pass ? 0.469 : 0.67;
                for (int sh = 16; sh >= 0; sh -= 8) {
                    double e = (double)(pal[q] >> sh & 255) - (double)(pal[i] >> sh & 255) * k2;
                    d += e * e;
                }
                if (d < best[pass]) best[pass] = d;
                if (q == dark[i]) got[pass] = d;
                if (!pass && q == i) self = d;
            }
        int nearest0 = got[0] <= best[0] * 1.0001 + 1e-3;
        int self_nearest = i < keep && self <= best[0] * 1.0001 + 1e-3;
        int nearest1 = got[1] <= best[1] * 1.0001 + 1e-3;
        wrong += !nearest0 && !(self_nearest && nearest1);
    }
    return wrong;
}

/* a frame against a reference of tools/ui_reference.py: `want` (palette indices) with `dk` passes of the table over each pixel afterwards; if
   `ov` is given, `want` is only an overlay (drawn where ov is 1), `base` the picture under it, darkened by dk (the order of the original's
   frame: the picture through the table first, the text and sprites over it). The number of pixels that differ; the first one is reported. */
static int compare_frame(const uint8_t *got, const uint8_t *want, const uint8_t *dk, const uint8_t *ov, const uint8_t *base, const uint8_t *table,
                         const char *what)
{
    int differ = 0, first = -1;
    for (int i = 0; i < S2G_PIC_BYTES; i++) {
        uint8_t e = ov ? (ov[i] ? want[i] : base[i]) : want[i];
        if (!ov || !ov[i])
            for (int p = 0; dk && p < dk[i]; p++) e = table[e];
        if (got[i] != e && differ++ == 0) first = i;
    }
    CHECK(!differ, "%s: %d pixels differ from the reference, first at x %d y %d (%02x, expected %02x)", what, differ, first % 320, first / 320,
          first >= 0 ? got[first] : 0, first >= 0 ? (ov && ov[first] ? want[first] : 0) : 0);
    return differ;
}

static uint8_t *ref_file(const char *ref, const char *name, const char *ext, uint32_t expect)
{
    char path[512];
    uint32_t n;
    snprintf(path, sizeof path, "%s/%s.%s", ref, name, ext);
    uint8_t *b = slurp(path, &n);
    CHECK(b && n == expect, "reference %s missing or of the wrong size", path);
    if (b && n != expect) { free(b); b = 0; }
    return b;
}

/* the best times of rows.txt into the save */
static void fill_save(const char *ref)
{
    char path[512], word[16], name[8];
    snprintf(path, sizeof path, "%s/rows.txt", ref);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "no rows.txt");
    s2save_reset();
    int kind, track, list, team, car, row, n, rows = 0;
    double t;
    while (f && fscanf(f, "%15s", word) == 1) {
        if (!strcmp(word, "add") && fscanf(f, "%d %d %d %lf %7s %d %d", &kind, &track, &list, &t, name, &team, &car) == 7) {
            s2save_add_time(kind, track, list, (float)t, name, team, car, 0);
            rows++;
        } else if (!strcmp(word, "replay") && fscanf(f, "%d %d %d %d %d", &kind, &track, &list, &row, &n) == 5) {
            s2save_set_replay(kind, track, list, row, n);
        }
    }
    if (f) fclose(f);
    CHECK(rows > 30, "rows.txt gave %d rows", rows);
}

static void put_keys(char *script, size_t size, const char *spec)
{
    memset(script, '.', size);
    script[size - 1] = 0;
    for (const char *s = spec; *s;) {                       /* "<frame>:<key> ...": the key's character in that frame */
        int at = atoi(s);
        s = strchr(s, ':') + 1;
        script[at] = *s++;
        while (*s == ' ') s++;
    }
}

static void check_settings(const char *ref, const char *prefix)
{
    static uint8_t dummy[256];
    char script[256];
    s2save_reset();
    /* frames: 0 .. 1 nothing (the page as it opens), D D (cursor on NUMBER OF LAPS), B B (laps 3 -> 5 -> 10), then D x 6 to VIEW HIGH SCORES, one
       more D (no wrap), a lit and a dark frame, A (leave); one key a frame, a held key is one press, so the keys stand on alternate frames */
    put_keys(script, sizeof script, "2:D 4:D 6:B 8:B 11:D 13:D 15:D 17:D 19:D 21:D 23:D 44:A");
    begin_run(script, prefix);
    cap_at(2, 0);
    cap_at(11, 1);
    cap_at(32, 2);
    cap_at(42, 3);
    shot_at(2, "settings_a");
    shot_at(11, "settings_b");
    shot_at(32, "settings_c");
    shot_at(42, "settings_d");
    s2_page_settings();
    static const char *const names[4] = { "settings_0", "settings_1", "settings_2", "settings_3" };
    for (int k = 0; k < 4; k++) {
        uint8_t *want = ref_file(ref, names[k], "idx", S2G_PIC_BYTES);
        if (want) compare_frame(cap[k], want, 0, 0, 0, dummy, names[k]);
        free(want);
    }
    static const int expect_clicks[10] = { 10, 10, 12, 12, 10, 10, 10, 10, 10, 10 };
    int clicks_ok = nclicks == 10;
    for (int i = 0; i < 10 && i < nclicks; i++) clicks_ok &= clicks[i] == expect_clicks[i];
    CHECK(clicks_ok, "GAME SETTINGS clicks: %d, the first %d %d %d %d", nclicks, clicks[0], clicks[1], clicks[2], clicks[3]);
    CHECK(s2save_get(S2S_LAPS) == 3 && s2save_get(S2S_TRANS) == 0 && s2save_get(S2S_SKILL) == 1 && s2save_get(S2S_DAMAGE) == 0,
          "the settings in the save: laps %d (3 expected)", s2save_get(S2S_LAPS));
    CHECK(swaps >= 43 && swaps <= 45, "the page ran %u frames (ESC at 44 is a cut)", swaps);

    /* left / right step the value back and on; every row of the save is reachable and wraps; the eight settings and their value texts */
    s2save_reset();
    put_keys(script, sizeof script, "2:R 4:R 6:L 8:D 10:R 12:R 14:L 16:A");
    begin_run(script, prefix);
    s2_page_settings();
    CHECK(s2save_get(S2S_TRANS) == 1 && s2save_get(S2S_DETAIL) == 0 && nclicks == 7, "left / right: transmission %d, detail %d, %d clicks",
          s2save_get(S2S_TRANS), s2save_get(S2S_DETAIL), nclicks);
    /* the cursor does not wrap, ESC is silent, the last row's B goes to the scores (click 13) and the way back is the settings again */
    s2save_reset();
    put_keys(script, sizeof script, "1:U 3:D 5:D 7:D 9:D 11:D 13:D 15:D 17:D 19:D 21:B 40:A 80:A 120:A");
    begin_run(script, prefix);
    s2_page_settings();
    CHECK(nclicks == 9 && clicks[7] == 10 && clicks[8] == S2PAGE_SND_BACK, "VIEW HIGH SCORES: %d clicks, the last %d", nclicks, clicks[nclicks - 1]);
    CHECK(swaps >= 117 && swaps <= 121, "settings, scores and settings again: %u frames (the scores fade out after B at 21; the two ESC are cuts)", swaps);
    printf("ui: GAME SETTINGS: four states equal to the reference, keys, clicks and the save checked\n");
}

static void check_hiscores(const char *ref, const char *prefix)
{
    static uint32_t pal[256];
    static uint8_t table[256];
    char script[256];
    fill_save(ref);
    CHECK(s2gfx_bkk("HISCOREL.BKK", s2gfx_pic(1), pal), "HISCOREL.BKK");
    s2gfx_dark_table(pal, table, 256);
    CHECK(!table_wrong(pal, table, 256), "darkening table of HISCOREL");
    /* the first visit: the arcade totals of 3 laps on England; D x 4 (row 5), R (the kind box), B (CHAMPIONSHIP, an empty list), B B (TIME ATTACK,
       ARCADE), L, B (LAP TIME), U x 4 (the first row, which has a replay), R x 3 and B (the track: Egypt), R (VIEW REPLAY), then dark, D x 14, L x 4,
       B (TOTAL TIME), R x 3, B x 6 (round to England), L x 3: the last row in view; A leaves. One key a frame, on alternate frames. */
    put_keys(script, sizeof script, "2:D 4:D 6:D 8:D 10:R 12:B 14:B 16:B 18:L 20:B 22:U 24:U 26:U 28:U 30:R 32:R 34:R 36:B 38:R "
             "58:D 60:D 62:D 64:D 66:D 68:D 70:D 72:D 74:D 76:D 78:D 80:D 82:D 84:D 86:L 88:L 90:L 92:L 94:B 96:R 98:R 100:R "
             "102:B 104:B 106:B 108:B 110:B 112:B 114:L 116:L 118:L 122:A");
    begin_run(script, prefix);
    static const struct { uint32_t call; const char *name; const char *png; } at[9] = {
        { 2, "hs_0", "scores_0" }, { 12, "hs_1", "scores_1" }, { 14, "hs_2", "scores_2" }, { 22, "hs_3", "scores_3" }, { 30, "hs_4", "scores_4" },
        { 38, "hs_5", "scores_5" }, { 40, "hs_6", "scores_6" }, { 57, "hs_7", "scores_7" }, { 121, "hs_8", "scores_8" } };
    for (int k = 0; k < 9; k++) {
        cap_at(at[k].call, k);
        shot_at(at[k].call, at[k].png);
    }
    s2_page_hiscores();
    for (int k = 0; k < 9; k++) {
        uint8_t *want = ref_file(ref, at[k].name, "idx", S2G_PIC_BYTES), *dk = ref_file(ref, at[k].name, "dk", S2G_PIC_BYTES);
        if (want && dk) compare_frame(cap[k], want, dk, 0, 0, table, at[k].name);
        free(want);
        free(dk);
    }
    CHECK(nclicks == 0, "the scores page is silent (%d clicks)", nclicks);
    CHECK(swaps >= 121 && swaps <= 123, "the page ran %u frames (ESC at 122 is a cut)", swaps);
    printf("ui: HI SCORES: nine states (lists, boxes, scrolling, the replay box lit and dark) equal to the reference, the keys checked\n");
}

/* the end of the race over a race picture: the three runs of ui_reference.py's END_RUNS, frame by frame (one video frame each) */
static void check_end(const char *ref, const char *prefix)
{
    static struct {
        const char *name, *race; struct s2_end_in in; int keep[6], right_at, last;
    } runs[3] = {
        { "A", "s2_track4_city", { 4, 3, 1, 85.5f, 29.5f, 2, 3 }, { 100, 200, 231, 302, 400, -1 }, 300, 400 },
        { "B", "s2_track1_village", { 1, 3, 3, 140.125f, 45.0f, 4, 2 }, { 100, 400, -1 }, -1, 400 },
        { "C", "s2_track5_snow", { 7, 1, 4, 99.0f, 0.0f, 1, 1 }, { 400, -1 }, -1, 400 },
    };
    static uint32_t pal[256];
    static uint8_t table[256];
    shot_prefix = prefix;
    fill_save(ref);
    for (int r = 0; r < 3; r++) {
        char name[64];
        snprintf(name, sizeof name, "frame_%s", runs[r].race);
        uint8_t *base = ref_file(ref, name, "idx", S2G_PIC_BYTES);
        uint8_t *pb = ref_file(ref, name, "pal", 1024);
        if (!base || !pb) continue;
        memcpy(pal, pb, 1024);
        for (int i = 0; i < 256; i++) VID_PAL[i] = pal[i];
        s2gfx_dark_table(pal, table, 256);
        CHECK(!table_wrong(pal, table, 256), "darkening table of the race palette of %s", runs[r].race);
        int result = 0, early = 0;
        for (int j = 0; j <= runs[r].last; j++) {
            memcpy((void *)FB(0), base, S2G_PIC_BYTES);
            uint32_t press = j == runs[r].right_at ? KEY_RIGHT : j == 50 ? KEY_B : 0;
            int got = s2end_frame(FB(0), 0, pal, &runs[r].in, (uint32_t)j, 1000 + (uint32_t)j, press);
            if (j == 50) early = got;
            for (int k = 0; runs[r].keep[k] >= 0; k++)
                if (runs[r].keep[k] == j) {
                    char what[40], png[40];
                    snprintf(what, sizeof what, "end_%s_%d", runs[r].name, j);
                    uint8_t *want = ref_file(ref, what, "idx", S2G_PIC_BYTES), *dk = ref_file(ref, what, "dk", S2G_PIC_BYTES),
                            *ov = ref_file(ref, what, "ov", S2G_PIC_BYTES);
                    if (want && dk && ov) compare_frame((const uint8_t *)FB(0), want, dk, ov, base, table, what);
                    free(want);
                    free(dk);
                    free(ov);
                    front = 0;
                    snprintf(png, sizeof png, "end_%s_%d", runs[r].name, j);
                    save(png);
                }
        }
        CHECK(early == S2END_NONE, "run %s: ENTER in the first four seconds took effect (%d)", runs[r].name, early);
        /* ENTER: the lit choice: RACE AGAIN, or EXIT after right */
        result = s2end_frame(FB(0), 0, pal, &runs[r].in, (uint32_t)runs[r].last + 1, 1000 + (uint32_t)runs[r].last + 1, r == 2 ? KEY_START : KEY_B);
        CHECK(result == (runs[r].right_at >= 0 ? S2END_EXIT : S2END_AGAIN), "run %s: ENTER gave %d", runs[r].name, result);
        free(base);
        free(pb);
    }
    /* the lists as the end screens left them: the player's name, team and car, the total in the list of the laps, the lap in the lap list */
    const struct s2save_row *t = s2save_row(S2S_KIND_ARCADE, 4, 1, 6), *l = s2save_row(S2S_KIND_ARCADE, 4, S2S_LAP, 0);
    CHECK(t->time == 85.5f && !strcmp(t->name, "MIL") && t->team == 2 && t->car == 3 && t->replay == 0, "track 4 row 7: %g %s %d %d", t->time, t->name, t->team, t->car);
    CHECK(l->time == 29.5f && l->team == 2 && l->car == 3, "track 4 best lap: %g", l->time);
    const struct s2save_row *b = s2save_row(S2S_KIND_ARCADE, 1, 1, 7), *c = s2save_row(S2S_KIND_ARCADE, 7, 0, 14);
    CHECK(b->time == 140.125f && b->team == 4 && b->car == 2, "track 1 row 8: %g", b->time);
    CHECK(c->time == 64.0f && s2save_row(S2S_KIND_ARCADE, 7, 0, 0)->time == 50.0f, "a slower time than the fifteenth goes nowhere (row 15 is %g)", c->time);
    CHECK(s2save_row(S2S_KIND_ARCADE, 7, S2S_LAP, 0)->time >= S2S_NO_TIME, "a run without a best lap writes none");
    /* a time equal to one in the list goes ahead of it (0x354cf: fcomp, ja): 83.01 against DDD and CCC (the later one went ahead of the first in
       rows.txt) of track 1's 3-lap list; the race's frames up to the choices (200 of them) are enough to put it in */
    struct s2_end_in tie = { 1, 3, 2, (float)83.01, 0.0f, 1, 1 };
    for (int j = 0; j <= 200; j++) s2end_frame(FB(1), 1, pal, &tie, (uint32_t)j, 5000 + (uint32_t)j, 0);
    CHECK(s2save_row(S2S_KIND_ARCADE, 1, 1, 2)->time == tie.time && !strcmp(s2save_row(S2S_KIND_ARCADE, 1, 1, 2)->name, "MIL") &&
          !strcmp(s2save_row(S2S_KIND_ARCADE, 1, 1, 3)->name, "DDD") && !strcmp(s2save_row(S2S_KIND_ARCADE, 1, 1, 4)->name, "CCC") &&
          !strcmp(s2save_row(S2S_KIND_ARCADE, 1, 1, 1)->name, "BBB"), "a tied time goes ahead of the old ones");
    /* the loading screen's BEST TIME / BEST LAP from the same lists */
    struct s2_record total, lap;
    s2_best_get(4, 3, &total, &lap);
    CHECK(total.time == 70.0f && !strcmp(total.name, "ONE") && lap.time == 29.5f, "BEST TIME of track 4: %g %s, BEST LAP %g", total.time, total.name, lap.time);
    s2_best_get(5, 3, &total, &lap);
    CHECK(total.time >= S2S_NO_TIME && !strcmp(total.name, "MIL") && lap.time >= S2S_NO_TIME, "an empty track has no record");
    s2_best_get(1, 10, &total, &lap);
    CHECK(total.time >= S2S_NO_TIME, "the 10-lap list of track 1 is empty");
    char tx[24];
    s2_best_get(1, 3, &total, &lap);
    s2_record_text(tx, &total);
    CHECK(!strcmp(tx, "01:20:50 AAA"), "BEST TIME text '%s'", tx);
    printf("ui: end of the race: three runs of frames equal to the reference, ENTER, the best times and the loading screen's records checked\n");
}

/* the start menu's two rows open the pages and take the settings back */
static void check_menu_hook(const char *prefix)
{
    char script[400];
    struct s2_menu_cfg c;
    s2save_reset();
    s2save_set(S2S_PROFILE, 1);                                     /* (a new save shows TEAM SELECTION before the menu: its own test, check_pages3d) */
    s2menu_load(&c);
    /* five D from ARCADE to GAME SETTINGS, A; in the page D D B (laps 3 -> 5), A out (a cut); D to HI SCORES, A; A out; U U U to START RACE, A */
    put_keys(script, sizeof script, "2:D 4:D 6:D 8:D 10:D 12:A 16:D 18:D 20:B 22:A 30:D 32:A 40:A 46:U 48:U 50:U 52:A");
    begin_run(script, prefix);
    int r = s2menu_run(&c);
    CHECK(r == S2M_RACE, "the menu ended with %d", r);
    CHECK(s2_menu_laps[c.laps] == 5 && s2save_get(S2S_LAPS) == 2, "the menu took the laps back from the page: %d", s2_menu_laps[c.laps]);
    CHECK(c.track == 1, "track %d", c.track);
    printf("ui: start menu: GAME SETTINGS and HI SCORES rows open the pages, the settings come back\n");
}

/* the track's banner of the race screen as the toolkit draws it into the first framebuffer's rows, a copy in out */
static void draw_banner(int track, uint8_t *out)
{
    static struct blit_list l;
    blit_start(&l, 0);
    memset((void *)FB(0), 0, S2G_PIC_BYTES);
    s2gfx_sprite_centred(&l, s2gfx_banner(track), 4);
    blit_model(FB(0), &l);
    memcpy(out, (const void *)FB(0), S2G_PIC_BYTES);
}

/* the pages with 3D objects (TEAM SELECTION, ENTER YOUR NAME, the CAR page): pages3d_screen.c, with the stand-ins of the geometry unit */
#include "pages3d_screen.c"

/* ------------------------------------------------------------------------------------------------ all of it */

int ui_tests(struct zip *z, struct zip *deflated, const char *ref, const char *prefix)
{
    failures = 0;
    *(volatile uint32_t *)0xF000000Cu = 0x1Fu;                      /* MEM_STATUS: all writes done, the SDRAM ready */
    IN_KEY = 0;
    dlog_init();
    CHECK(s2gfx_init(z), "s2gfx_init on the stored ZIP");
    check_pictures(ref, "stored ZIP");
    check_text(ref, prefix);
    check_loading(prefix);
    check_pause(ref, prefix);
    check_settings(ref, prefix);
    check_hiscores(ref, prefix);
    check_end(ref, prefix);
    check_menu_hook(prefix);
    check_runner();
    check_arcade_cases(ref, prefix);
    check_arcade_chain(prefix);
    check_arcade_reach(prefix);
    check_pages3d(prefix);
    check_sounds(z, ref);
    static uint8_t banners[7][S2G_PIC_BYTES];                       /* the race screen's track banners, drawn on the stored ZIP's init */
    int stored_banners = s2gfx_banner(1) != 0x13;
    for (int k = 0; k < 7; k++) {
        draw_banner(k + 1, banners[k]);
        int drawn = 0;
        for (int i = 0; i < S2G_PIC_BYTES; i++) drawn += banners[k][i] != 0;
        CHECK(drawn > 3000 && drawn <= 144 * 50, "track %d's banner has %d pixels", k + 1, drawn);
    }
    CHECK(stored_banners && s2gfx_banner(2) != 0x14, "GAMEL.SPR's banners are not in the table (%d)", s2gfx_banner(1));
    if (deflated) {                                                 /* GRAF1.DAT inflated whole at the start, the pictures out of that */
        CHECK(s2gfx_init(deflated), "s2gfx_init on the deflated ZIP");
        check_pictures(ref, "deflated ZIP");
        char name[16] = "LOAD_3L.BKK";
        static uint32_t pal[256];
        CHECK(s2gfx_bkk(name, s2gfx_pic(0), pal), "a picture from the deflated ZIP");
        CHECK(s2load_show(3, 3) != 0, "loading screen from the deflated ZIP");
        static uint8_t again[S2G_PIC_BYTES];                        /* the same banners out of a deflated GAME1.DAT, inflated whole */
        for (int k = 0; k < 7; k++) {
            draw_banner(k + 1, again);
            CHECK(!memcmp(again, banners[k], S2G_PIC_BYTES), "track %d's banner differs in the deflated ZIP", k + 1);
        }
        printf("ui: GAMEL.SPR's seven track banners: the same from a stored and a deflated GAME1.DAT\n");
    }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    printf(failures ? "ui: %d tests FAILED\n" : "ui: all tests passed\n", failures);
    return failures;
}
