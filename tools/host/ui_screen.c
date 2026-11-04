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
#include "../s2snd.h"
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
static const char *script;              /* the buttons of each call: one character a frame, ' ' or '.' none, U D L R A B S(tart) s(elect) X */
static uint32_t script_at;
static const char *shot_prefix;
static struct { uint32_t at; const char *name; } shots[16];
static int nshots;
static uint32_t pal_trace[128][2];       /* for the fade tests: palette entries 17 and 241 as each frame left them */
static uint32_t swaps;

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
static int music_frames, effects_calls, clicks[16], nclicks;
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
    if (nclicks < 16) clicks[nclicks++] = id;
}

static void begin_run(const char *keys, const char *prefix)
{
    script = keys;
    script_at = 0;
    IN_KEY = 0;
    calls = 0;
    swaps = 0;
    nshots = 0;
    shot_prefix = prefix;
    music_frames = effects_calls = nclicks = 0;
    effects_min = 0x8000;
}

static void shot_at(uint32_t call, const char *name)
{
    shots[nshots].at = call;
    shots[nshots++].name = name;
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
    check_runner();
    check_sounds(z, ref);
    if (deflated) {                                                 /* GRAF1.DAT inflated whole at the start, the pictures out of that */
        CHECK(s2gfx_init(deflated), "s2gfx_init on the deflated ZIP");
        check_pictures(ref, "deflated ZIP");
        char name[16] = "LOAD_3L.BKK";
        static uint32_t pal[256];
        CHECK(s2gfx_bkk(name, s2gfx_pic(0), pal), "a picture from the deflated ZIP");
        CHECK(s2load_show(3, 3) != 0, "loading screen from the deflated ZIP");
    }
    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    printf(failures ? "ui: %d tests FAILED\n" : "ui: all tests passed\n", failures);
    return failures;
}
