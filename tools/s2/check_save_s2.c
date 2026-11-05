/*
 * Host test of the save (firmware/s2save.c), run by tools/s2/check_save_s2.sh: the firmware's own code, with the data slot calls
 * and the debug log replaced by a fake slot (a file image, a size, a read that can fail) and the SDRAM window of CHOICE.DAT mapped
 * at its address. No game files are needed.
 *
 *   defaults       an empty slot, the layout's bytes checked against the table in docs/screamer2/menus.md, the unlocks
 *   interface      wrong keys and numbers, clamping, names, set-ups
 *   best times     rank, ties (an equal time goes ahead), the 15 rows, kinds / tracks / lists apart, time attack's one total list,
 *                  names / teams / cars / replays, wrong arguments; 20,000 random insertions against a plain model
 *   round trip     everything changed, "power off" (the window is the file), "power on" (the window scribbled, the file read)
 *   migration      the old layout (Screamer 1's block with the start menu's choices in its unused fields), with and without its
 *                  marker, with values out of range; the file written out afterwards loads as the new layout
 *   robustness     empty, short, foreign, random, oversized and huge files, a failing read, every single byte of a valid file
 *                  damaged (the damaged part falls back to the defaults and the other keeps its data), damage with a matching
 *                  checksum but values out of range, random multi-byte damage; after every load the save is sealed (checksums
 *                  match), every value is in range, the read never went past the file or the layout, ATTACK.DAT's window is untouched
 *
 * Exit status 1 on the first group with a failed check.
 */
#define _GNU_SOURCE
#include "../../firmware/s2save.h"
#include "../../firmware/save.h"
#include "../../firmware/champ.h"
#include "../../firmware/slot.h"
#include "../../firmware/dlog.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ------------------------------------------------------------------------------------------------ the fake slot and log */

static uint8_t file[1 << 20];
static uint32_t file_size, set_size, set_size_calls, read_calls;
static int read_fails, read_bad;
static char log_text[512];

uint32_t slot_size(uint32_t id)
{
    return id == SAVE_CHOICE_SLOT ? file_size : 0;
}

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    read_calls++;
    /* the APF's rules: a word-sized read inside the file; ours inside the layout's window as well */
    if (id != SAVE_CHOICE_SLOT || off != 0 || (len & 3) || len == 0 || len > file_size || len > S2SAVE_SIZE || addr != SAVE_CHOICE_ADDR) {
        read_bad++;
        return 2;
    }
    if (read_fails) return 2;
    memcpy((void *)(uintptr_t)addr, file, len);
    return 0;
}

void slot_set_size(uint32_t id, uint32_t size)
{
    if (id == SAVE_CHOICE_SLOT) {
        set_size = size;
        set_size_calls++;
    }
}

void dlog_str(const char *s)
{
    strncat(log_text, s, sizeof log_text - strlen(log_text) - 1);
}

void dlog_dec(int32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    dlog_str(b);
}

/* ------------------------------------------------------------------------------------------------ helpers */

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 30) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint8_t *const win = (uint8_t *)(uintptr_t)SAVE_CHOICE_ADDR;
static uint8_t *const guard = (uint8_t *)(uintptr_t)SAVE_ATTACK_ADDR;       /* ATTACK.DAT's window: must stay as the test left it */
#define GUARD_BYTES 4096

/* an independent CRC-32 (the firmware's goes half a byte at a time; this one a table of 256, made bit by bit) */
static uint32_t crc(const uint8_t *p, uint32_t n)
{
    static uint32_t t[256];
    if (!t[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

static uint32_t rng_state = 12345;
static uint32_t rnd(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* the keys' defaults and ranges as the layout table in the doc has them, written out again here */
static const int key_def[S2S_KEYS] = { 0, 2, 1, 1, 0, 0, 0, 0,  2,  0x6000, 0x6000, 0x6000,  1, 1, 1,  0, 0,  0 };
static const int key_lo[S2S_KEYS] = { 0, 0, 0, 0, 0, 0, 0, 0,  1,  0, 0, 0,  1, 1, 1,  0, 0,  0 };
static const int key_hi[S2S_KEYS] = { 1, 2, 3, 2, 1, 1, 1, 1,  7,  0x7FFF, 0x7FFF, 0x7FFF,  7, 4, 5,  3, 4,  1 };

/* "power on" with this file: the window is scribbled first, the way the SDRAM holds whatever it held, then s2save_load() */
static void power_on(const uint8_t *img, uint32_t n)
{
    memcpy(file, img, n);
    file_size = n;
    memset(win, 0xCC, S2SAVE_SIZE);
    log_text[0] = 0;
    set_size = 0;
    set_size_calls = 0;
    read_calls = 0;
    s2save_load();
}

static int name_printable(const char *n)
{
    return strlen(n) == 3 && n[0] >= ' ' && n[1] >= ' ' && n[2] >= ' ' && n[0] <= '~' && n[1] <= '~' && n[2] <= '~';
}

/* the save as it stands: sealed, in range, nothing written outside the layout */
static void check_sealed(const char *what)
{
    CHECK(rd32(win) == 0x56533253u && rd16(win + 4) == S2SAVE_VERSION && rd16(win + 6) == S2SAVE_SIZE, "%s: header", what);
    CHECK(rd32(win + 8) == crc(win + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE), "%s: main checksum", what);
    CHECK(rd32(win + 12) == crc(win + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE), "%s: times checksum", what);
    for (int k = 0; k < S2S_KEYS; k++) {
        int v = s2save_get(k);
        CHECK(v >= key_lo[k] && v <= key_hi[k], "%s: key %d = %d out of range", what, k, v);
        CHECK(s2save_count(k) == key_hi[k] - key_lo[k] + 1, "%s: count of key %d", what, k);
    }
    CHECK(name_printable(s2save_name()), "%s: name", what);
    for (int kind = 0; kind < S2S_KINDS; kind++)
        for (int t = 1; t <= S2S_TRACKS; t++)
            for (int l = 0; l < S2S_LISTS; l++)
                for (int r = 0; r < S2S_ROWS; r++) {
                    const struct s2save_row *p = s2save_row(kind, t, l, r);
                    if (!p || !(p->time >= 0.0f && p->time <= S2S_NO_TIME) || p->team < 1 || p->team > 4 || p->car < 1 || p->car > 5 || !name_printable(p->name)) {
                        CHECK(0, "%s: row %d/%d/%d/%d invalid", what, kind, t, l, r);
                        return;
                    }
                }
    CHECK(set_size == S2SAVE_SIZE && set_size_calls > 0, "%s: slot size %u", what, set_size);
    CHECK(read_bad == 0, "%s: a read outside the rules", what);
    for (int i = 0; i < GUARD_BYTES; i++)
        if (guard[i] != 0x5A) { CHECK(0, "%s: ATTACK.DAT's window touched at +%d", what, i); break; }
    for (int i = S2SAVE_SIZE; i < 0x10000; i++)
        if (win[i] != 0x5A) { CHECK(0, "%s: the window past the layout touched at +%d", what, i); break; }
}

/* the cheap part of it, for the loops over every byte */
static int sealed_quick(void)
{
    if (rd32(win) != 0x56533253u || rd16(win + 4) != S2SAVE_VERSION || rd16(win + 6) != S2SAVE_SIZE) return 0;
    if (rd32(win + 8) != crc(win + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE) || rd32(win + 12) != crc(win + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE)) return 0;
    for (int i = 0; i < GUARD_BYTES; i++)
        if (guard[i] != 0x5A) return 0;
    for (int i = S2SAVE_SIZE; i < 0x10000; i++)
        if (win[i] != 0x5A) return 0;
    return set_size == S2SAVE_SIZE && read_bad == 0;
}

static int has(const char *s)
{
    return strstr(log_text, s) != 0;
}

static void expect_defaults_main(const char *what)
{
    for (int k = 0; k < S2S_KEYS; k++) CHECK(s2save_get(k) == key_def[k], "%s: key %d is %d, default %d", what, k, s2save_get(k), key_def[k]);
    CHECK(!strcmp(s2save_name(), "MIL"), "%s: name %s", what, s2save_name());
    for (int t = 1; t <= 4; t++) {
        CHECK(win[S2SAVE_MAIN_OFF + 32 + 100 * (t - 1)] == 0, "%s: bonus car of team %d", what, t);
        for (int c = 1; c <= 4; c++)
            for (int s = 0; s < 4; s++)
                CHECK(!memcmp(s2save_setup(t, c, s), "\2\2\0\2\2\2", 6), "%s: set-up %d/%d/%d", what, t, c, s);
    }
}

static void expect_defaults_times(const char *what)
{
    for (int kind = 0; kind < S2S_KINDS; kind++)
        for (int t = 1; t <= S2S_TRACKS; t++)
            for (int l = 0; l < S2S_LISTS; l++)
                for (int r = 0; r < S2S_ROWS; r++) {
                    const struct s2save_row *p = s2save_row(kind, t, l, r);
                    CHECK(s2save_row_empty(p) && p->time == S2S_NO_TIME && !strcmp(p->name, "MIL") && p->team == (r & 3) + 1 && p->car == 1 && p->replay == 0,
                          "%s: row %d/%d/%d/%d is not the empty row", what, kind, t, l, r);
                }
}

/* ------------------------------------------------------------------------------------------------ tests */

static uint8_t good[S2SAVE_SIZE];       /* a valid, fully used save: everything changed */

static void test_defaults(void)
{
    power_on(file, 0);
    check_sealed("empty slot");
    CHECK(has("none: defaults") && has("(0 bytes)"), "log '%s'", log_text);
    CHECK(read_calls == 0, "an empty slot is not read");
    expect_defaults_main("defaults");
    expect_defaults_times("defaults");

    /* the layout's bytes, from the table in docs/screamer2/menus.md */
    CHECK(!memcmp(win, "S2SV", 4) && rd16(win + 4) == 1 && rd16(win + 6) == 19348, "header bytes");
    CHECK(S2SAVE_MAIN_OFF == 16 && S2SAVE_MAIN_SIZE == 432 && S2SAVE_TIMES_OFF == 448 && S2SAVE_TIMES_SIZE == 18900 && S2SAVE_SIZE == 19348, "constants");
    CHECK(!memcmp(win + 16, "MIL", 4), "name at +16");
    static const uint8_t set[8] = { 0, 2, 1, 1, 0, 0, 0, 0 };
    CHECK(!memcmp(win + 20, set, 8), "settings at +20");
    CHECK(rd16(win + 28) == 0x6000 && rd16(win + 30) == 0x6000 && rd16(win + 32) == 0x6000, "volumes at +28");
    static const uint8_t picks[6] = { 2, 1, 1, 1, 0, 0 };       /* view, track, team, car, league open, tracks open at +34 */
    CHECK(!memcmp(win + 34, picks, 6), "view and picks at +34");
    CHECK(win[40] == 0, "the profile byte at +40 is 0 on a new save");
    for (int i = 41; i < 48; i++) CHECK(win[i] == 0, "reserved at +%d", i);
    for (int t = 0; t < 4; t++) {                               /* the teams from +48, 100 bytes each: car5, 4 x 4 x 6 set-up numbers, 3 pad */
        const uint8_t *p = win + 48 + 100 * t;
        CHECK(p[0] == 0 && p[97] == 0 && p[98] == 0 && p[99] == 0, "team %d record", t);
        for (int i = 0; i < 16; i++) CHECK(!memcmp(p + 1 + 6 * i, "\2\2\0\2\2\2", 6), "team %d set-up %d", t, i);
    }
    /* the times from +448: lists of 15 rows of 12 bytes [time f32][name 4][team][car][replay u16]; list = (kind * 7 + track - 1) * 5 + list */
    CHECK(rd32(win + 448) == 0x4560FFFCu && !memcmp(win + 452, "MIL", 4) && win[456] == 1 && win[457] == 1 && rd16(win + 458) == 0, "row 0 of the first list");
    CHECK(rd32(win + 448 + 12 * 3) == 0x4560FFFCu && win[448 + 12 * 3 + 8] == 4 && win[448 + 12 * 4 + 8] == 1, "teams of the empty rows run 1 .. 4");
    CHECK(rd32(win + 448 + 180) == 0x4560FFFCu && rd32(win + 448 + 180 * 105 - 12) == 0x4560FFFCu, "105 lists of 180 bytes end the file");

    /* the unlocks as data */
    CHECK(s2save_unlock_all == S2SAVE_UNLOCK_ALL, "the build's default is the variable's start");
    s2save_unlock_all = 0;
    for (int t = 1; t <= 7; t++) CHECK(s2save_track_open(t) == (t == 1 || t == 3 || t == 4), "track %d open at the start: %d", t, s2save_track_open(t));
    CHECK(s2save_track_open(0) == 0 && s2save_track_open(8) == 0, "no track 0 or 8");
    static const int order[7] = { 1, 3, 4, 5, 6, 2, 7 };
    for (int p = 0; p < 7; p++) CHECK(s2save_track_at(p) == order[p], "order %d", p);
    CHECK(s2save_track_at(-1) == 0 && s2save_track_at(7) == 0, "order out of range");
    for (int extra = 0; extra <= 4; extra++) {
        s2save_set(S2S_TRACKS_OPEN, extra);
        int n = 0;
        for (int t = 1; t <= 7; t++) n += s2save_track_open(t);
        CHECK(n == 3 + extra, "tracks open with %d extra: %d", extra, n);
        for (int p = 0; p < 7; p++) CHECK(s2save_track_open(order[p]) == (p < 3 + extra), "track %d with %d extra", order[p], extra);
    }
    s2save_set(S2S_TRACKS_OPEN, 9);
    CHECK(s2save_get(S2S_TRACKS_OPEN) == 4, "tracks open clamps");
    for (int l = 0; l < 4; l++) {
        s2save_set(S2S_LEAGUE_OPEN, l);
        for (int k = -1; k <= 4; k++) CHECK(s2save_league_open(k) == (k >= 0 && k <= l), "league %d open with %d", k, l);
    }
    s2save_set_car5(2, 1);
    CHECK(s2save_car5_open(2) && !s2save_car5_open(1) && !s2save_car5_open(0) && !s2save_car5_open(5), "bonus car");
    s2save_unlock_all = 1;
    for (int t = 1; t <= 7; t++) CHECK(s2save_track_open(t), "unlock all: track %d", t);
    CHECK(!s2save_track_open(0) && !s2save_track_open(8), "unlock all: still no track 0 or 8");
    for (int l = 0; l < 4; l++) CHECK(s2save_league_open(l), "unlock all: league %d", l);
    CHECK(s2save_car5_open(1) && !s2save_car5_open(0) && !s2save_car5_open(5), "unlock all: bonus cars");
    s2save_unlock_all = S2SAVE_UNLOCK_ALL;
    check_sealed("unlocks");
}

static void test_api(void)
{
    power_on(file, 0);
    CHECK(s2save_get(-1) == 0 && s2save_get(S2S_KEYS) == 0 && s2save_count(-1) == 0 && s2save_count(S2S_KEYS) == 0, "wrong keys");
    s2save_set(-1, 3);
    s2save_set(S2S_KEYS, 3);
    for (int k = 0; k < S2S_KEYS; k++) {
        s2save_set(k, -100000);
        CHECK(s2save_get(k) == key_lo[k], "key %d low clamp: %d", k, s2save_get(k));
        s2save_set(k, 100000);
        CHECK(s2save_get(k) == key_hi[k], "key %d high clamp: %d", k, s2save_get(k));
        s2save_set(k, key_hi[k] > 1 ? key_hi[k] - 1 : key_lo[k]);
        CHECK(s2save_get(k) == (key_hi[k] > 1 ? key_hi[k] - 1 : key_lo[k]), "key %d in range", k);
        check_sealed("set");
    }
    /* the start menu's ranges are the save's */
    CHECK(s2save_count(S2S_TRACK) == 7 && s2save_count(S2S_TEAM) == 4 && s2save_count(S2S_CAR) == 5 && s2save_count(S2S_LAPS) == 4 &&
          s2save_count(S2S_SKILL) == 3 && s2save_count(S2S_TRANS) == 2 && s2save_count(S2S_DETAIL) == 3, "counts of the pages' rows");

    s2save_set_name("ABC");
    CHECK(!strcmp(s2save_name(), "ABC"), "name");
    s2save_set_name("A");
    CHECK(!strcmp(s2save_name(), "A  "), "short name padded: '%s'", s2save_name());
    s2save_set_name("TOOLONG");
    CHECK(!strcmp(s2save_name(), "TOO"), "long name cut: '%s'", s2save_name());
    s2save_set_name("A\x01\xff");
    CHECK(!strcmp(s2save_name(), "A??"), "unprintable: '%s'", s2save_name());
    s2save_set_name(0);
    CHECK(!strcmp(s2save_name(), "   "), "no name: '%s'", s2save_name());
    check_sealed("name");

    int8_t six[6] = { -5, 9, 0, 1, 4, -1 };
    CHECK(s2save_set_setup(3, 2, 1, six) == 0, "set-up set");
    const int8_t *p = s2save_setup(3, 2, 1);
    CHECK(p && p[0] == -1 && p[1] == 4 && p[2] == 0 && p[3] == 1 && p[4] == 4 && p[5] == -1, "set-up clamped to -1 .. 4");
    CHECK(!memcmp(s2save_setup(3, 2, 0), "\2\2\0\2\2\2", 6) && !memcmp(s2save_setup(3, 1, 1), "\2\2\0\2\2\2", 6) && !memcmp(s2save_setup(2, 2, 1), "\2\2\0\2\2\2", 6),
          "the neighbours are untouched");
    CHECK(!s2save_setup(0, 1, 0) && !s2save_setup(5, 1, 0) && !s2save_setup(1, 0, 0) && !s2save_setup(1, 5, 0) && !s2save_setup(1, 1, -1) && !s2save_setup(1, 1, 4), "wrong set-up numbers");
    CHECK(s2save_set_setup(0, 1, 0, six) != 0 && s2save_set_setup(1, 1, 4, six) != 0, "wrong set-up numbers are refused");
    check_sealed("set-up");

    CHECK(!s2save_row(-1, 1, 0, 0) && !s2save_row(3, 1, 0, 0) && !s2save_row(0, 0, 0, 0) && !s2save_row(0, 8, 0, 0) && !s2save_row(0, 1, -1, 0) &&
          !s2save_row(0, 1, 5, 0) && !s2save_row(0, 1, 0, -1) && !s2save_row(0, 1, 0, 15), "wrong row numbers");
}

/* a plain model of the lists */
struct mrow { float time; char name[4]; int team, car, replay; };
static struct mrow model[S2S_KINDS][S2S_TRACKS + 1][S2S_LISTS][S2S_ROWS];

static void model_reset(void)
{
    for (int k = 0; k < S2S_KINDS; k++)
        for (int t = 1; t <= S2S_TRACKS; t++)
            for (int l = 0; l < S2S_LISTS; l++)
                for (int r = 0; r < S2S_ROWS; r++) model[k][t][l][r] = (struct mrow){ S2S_NO_TIME, "MIL", (r % 4) + 1, 1, 0 };
}

static int model_add(int kind, int track, int list, float time, const char *name, int team, int car, int replay)
{
    if (kind == S2S_KIND_ATTACK && list != S2S_LAP) list = 1;
    struct mrow *m = model[kind][track][list];
    if (!(time > 0.0f && time < S2S_NO_TIME)) return -1;
    int at = -1;
    for (int i = 0; i < S2S_ROWS; i++)
        if (time <= m[i].time) { at = i; break; }               /* the first row it is not slower than */
    if (at < 0) return -1;
    for (int i = S2S_ROWS - 1; i > at; i--) m[i] = m[i - 1];
    m[at].time = time;
    snprintf(m[at].name, 4, "%s", name);
    m[at].team = team;
    m[at].car = car;
    m[at].replay = replay;
    return at;
}

static int model_same(const char *what)
{
    for (int k = 0; k < S2S_KINDS; k++)
        for (int t = 1; t <= S2S_TRACKS; t++)
            for (int l = 0; l < S2S_LISTS; l++)
                for (int r = 0; r < S2S_ROWS; r++) {
                    const struct s2save_row *p = s2save_row(k, t, l, r);
                    const struct mrow *m = &model[k][t][k == S2S_KIND_ATTACK && l != S2S_LAP ? 1 : l][r];
                    if (p->time != m->time || strcmp(p->name, m->name) || p->team != m->team || p->car != m->car || p->replay != m->replay) {
                        CHECK(0, "%s: row %d/%d/%d/%d differs from the model (%g %s %d %d %d against %g %s %d %d %d)", what, k, t, l, r, p->time, p->name, p->team, p->car,
                              p->replay, m->time, m->name, m->team, m->car, m->replay);
                        return 0;
                    }
                }
    CHECK(1, "%s", what);
    return 1;
}

static void test_times(void)
{
    power_on(file, 0);
    model_reset();

    /* ranks, in a list that starts empty */
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 100.0f, "AAA", 1, 1, 0) == 0, "first time takes row 0");
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 120.0f, "BBB", 2, 2, 0) == 1, "slower takes row 1");
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 90.0f, "CCC", 3, 3, 7) == 0, "quicker takes row 0");
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 110.0f, "DDD", 4, 5, 0) == 2, "between takes row 2");
    const struct s2save_row *r = s2save_row(S2S_KIND_ARCADE, 3, 1, 0);
    CHECK(r->time == 90.0f && !strcmp(r->name, "CCC") && r->team == 3 && r->car == 3 && r->replay == 7, "row 0 is CCC");
    r = s2save_row(S2S_KIND_ARCADE, 3, 1, 2);
    CHECK(r->time == 110.0f && !strcmp(r->name, "DDD") && r->team == 4 && r->car == 5, "row 2 is DDD");
    CHECK(s2save_row_empty(s2save_row(S2S_KIND_ARCADE, 3, 1, 4)) && !s2save_row_empty(s2save_row(S2S_KIND_ARCADE, 3, 1, 3)), "rows 0..3 are used");
    CHECK(s2save_row(S2S_KIND_ARCADE, 3, 1, 4)->team == (4 % 4) + 1, "an empty row keeps its team");
    model_add(S2S_KIND_ARCADE, 3, 1, 100.0f, "AAA", 1, 1, 0);
    model_add(S2S_KIND_ARCADE, 3, 1, 120.0f, "BBB", 2, 2, 0);
    model_add(S2S_KIND_ARCADE, 3, 1, 90.0f, "CCC", 3, 3, 7);
    model_add(S2S_KIND_ARCADE, 3, 1, 110.0f, "DDD", 4, 5, 0);
    model_same("ranks");

    /* a tie goes ahead of the old time; the old one moves down */
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 100.0f, "TIE", 1, 1, 0) == 1, "an equal time takes the place of the old one");
    CHECK(!strcmp(s2save_row(S2S_KIND_ARCADE, 3, 1, 1)->name, "TIE") && !strcmp(s2save_row(S2S_KIND_ARCADE, 3, 1, 2)->name, "AAA"), "TIE ahead of AAA");
    model_add(S2S_KIND_ARCADE, 3, 1, 100.0f, "TIE", 1, 1, 0);
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 90.0f, "TI2", 1, 1, 0) == 0 && !strcmp(s2save_row(S2S_KIND_ARCADE, 3, 1, 1)->name, "CCC"), "an equal best time takes row 0");
    model_add(S2S_KIND_ARCADE, 3, 1, 90.0f, "TI2", 1, 1, 0);

    /* the list holds fifteen: fill it, then a slower time is out, a quicker one pushes the last off, an equal last takes its place */
    for (int i = 0; model[S2S_KIND_ARCADE][3][1][14].time == S2S_NO_TIME; i++) {
        float t = 200.0f + (float)i;
        int got = s2save_add_time(S2S_KIND_ARCADE, 3, 1, t, "FIL", 2, 2, 0), want = model_add(S2S_KIND_ARCADE, 3, 1, t, "FIL", 2, 2, 0);
        CHECK(got == want, "fill %d: rank %d, model %d", i, got, want);
    }
    model_same("full list");
    float last = s2save_row(S2S_KIND_ARCADE, 3, 1, 14)->time;
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, last + 1.0f, "OUT", 1, 1, 0) == -1, "slower than the fifteenth is out");
    model_same("slower out");
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, 95.0f, "NEW", 1, 1, 0) == 2 && s2save_row(S2S_KIND_ARCADE, 3, 1, 14)->time < last, "a quicker one pushes the last off");
    model_add(S2S_KIND_ARCADE, 3, 1, 95.0f, "NEW", 1, 1, 0);
    model_same("pushed");
    last = s2save_row(S2S_KIND_ARCADE, 3, 1, 14)->time;
    CHECK(s2save_add_time(S2S_KIND_ARCADE, 3, 1, last, "EQL", 1, 1, 0) == 14 && !strcmp(s2save_row(S2S_KIND_ARCADE, 3, 1, 14)->name, "EQL"), "an equal fifteenth takes its place");
    model_add(S2S_KIND_ARCADE, 3, 1, last, "EQL", 1, 1, 0);
    model_same("equal last");
    for (int i = 1; i < 15; i++) CHECK(s2save_row(S2S_KIND_ARCADE, 3, 1, i - 1)->time <= s2save_row(S2S_KIND_ARCADE, 3, 1, i)->time, "sorted at %d", i);

    /* what is not a time */
    float notime[] = { 0.0f, -1.0f, S2S_NO_TIME, 4000.0f, NAN, INFINITY, -INFINITY };
    for (unsigned i = 0; i < sizeof notime / sizeof notime[0]; i++)
        CHECK(s2save_add_time(S2S_KIND_LEAGUE, 1, 0, notime[i], "BAD", 1, 1, 0) == -1, "time %g refused", notime[i]);
    CHECK(s2save_add_time(-1, 1, 0, 50.0f, "BAD", 1, 1, 0) == -1 && s2save_add_time(3, 1, 0, 50.0f, "BAD", 1, 1, 0) == -1 && s2save_add_time(0, 0, 0, 50.0f, "BAD", 1, 1, 0) == -1 &&
          s2save_add_time(0, 8, 0, 50.0f, "BAD", 1, 1, 0) == -1 && s2save_add_time(0, 1, -1, 50.0f, "BAD", 1, 1, 0) == -1 && s2save_add_time(0, 1, 5, 50.0f, "BAD", 1, 1, 0) == -1,
          "wrong kind, track or list refused");
    model_same("refused");
    CHECK(s2save_add_time(S2S_KIND_LEAGUE, 1, 0, 3599.0f, "OK1", 1, 1, 0) == 0, "3599 s is a time");
    model_add(S2S_KIND_LEAGUE, 1, 0, 3599.0f, "OK1", 1, 1, 0);

    /* the lists are apart: kinds, tracks, lists */
    CHECK(s2save_add_time(S2S_KIND_LEAGUE, 3, 1, 50.0f, "LEA", 1, 1, 0) == 0 && s2save_add_time(S2S_KIND_ARCADE, 4, 1, 50.0f, "TRK", 1, 1, 0) == 0 &&
          s2save_add_time(S2S_KIND_ARCADE, 3, 0, 50.0f, "LST", 1, 1, 0) == 0 && s2save_add_time(S2S_KIND_ARCADE, 3, S2S_LAP, 50.0f, "LAP", 1, 1, 0) == 0, "each into an empty list");
    model_add(S2S_KIND_LEAGUE, 3, 1, 50.0f, "LEA", 1, 1, 0);
    model_add(S2S_KIND_ARCADE, 4, 1, 50.0f, "TRK", 1, 1, 0);
    model_add(S2S_KIND_ARCADE, 3, 0, 50.0f, "LST", 1, 1, 0);
    model_add(S2S_KIND_ARCADE, 3, S2S_LAP, 50.0f, "LAP", 1, 1, 0);
    model_same("apart");

    /* time attack keeps only the 3-lap totals (and the laps) */
    CHECK(s2save_add_time(S2S_KIND_ATTACK, 5, 3, 70.0f, "TA1", 1, 1, 0) == 0, "time attack, a 10-lap total");
    model_add(S2S_KIND_ATTACK, 5, 3, 70.0f, "TA1", 1, 1, 0);
    CHECK(s2save_row(S2S_KIND_ATTACK, 5, 1, 0) == s2save_row(S2S_KIND_ATTACK, 5, 0, 0) && s2save_row(S2S_KIND_ATTACK, 5, 1, 0) == s2save_row(S2S_KIND_ATTACK, 5, 3, 0),
          "time attack's totals are one list");
    CHECK(!strcmp(s2save_row(S2S_KIND_ATTACK, 5, 1, 0)->name, "TA1"), "it is the 3-lap list");
    CHECK(s2save_add_time(S2S_KIND_ATTACK, 5, S2S_LAP, 20.0f, "TA2", 1, 1, 0) == 0 && s2save_row(S2S_KIND_ATTACK, 5, S2S_LAP, 0) != s2save_row(S2S_KIND_ATTACK, 5, 1, 0),
          "time attack's laps are their own");
    model_add(S2S_KIND_ATTACK, 5, S2S_LAP, 20.0f, "TA2", 1, 1, 0);
    model_same("time attack");

    /* the fields */
    s2save_set_name("XYZ");
    CHECK(s2save_add_time(S2S_KIND_LEAGUE, 6, 2, 60.0f, 0, 9, 0, 70000) == 0, "no name, wild numbers");
    r = s2save_row(S2S_KIND_LEAGUE, 6, 2, 0);
    CHECK(!strcmp(r->name, "XYZ") && r->team == 4 && r->car == 1 && r->replay == 0, "the player's name, team and car clamped, replay out of range is none");
    CHECK(s2save_add_time(S2S_KIND_LEAGUE, 6, 2, 61.0f, "Q", 0, 6, 65535) == 1 && !strcmp(s2save_row(S2S_KIND_LEAGUE, 6, 2, 1)->name, "Q  ") &&
          s2save_row(S2S_KIND_LEAGUE, 6, 2, 1)->team == 1 && s2save_row(S2S_KIND_LEAGUE, 6, 2, 1)->car == 5 && s2save_row(S2S_KIND_LEAGUE, 6, 2, 1)->replay == 65535,
          "short name, clamped team and car, replay 65535");
    s2save_set_replay(S2S_KIND_LEAGUE, 6, 2, 0, 12);
    s2save_set_replay(S2S_KIND_LEAGUE, 6, 2, 15, 12);
    s2save_set_replay(S2S_KIND_LEAGUE, 6, 8, 0, 12);
    CHECK(s2save_row(S2S_KIND_LEAGUE, 6, 2, 0)->replay == 12 && s2save_row(S2S_KIND_LEAGUE, 6, 2, 1)->replay == 65535, "replay set on row 0 only");
    check_sealed("times");

    /* many random insertions against the model, with few different times so that ties are common */
    power_on(file, 0);
    model_reset();
    for (int i = 0; i < 20000; i++) {
        int kind = (int)(rnd() % 3), track = (int)(rnd() % 7) + 1, list = (int)(rnd() % 5);
        float time = (float)(rnd() % 40) * 0.5f + 1.0f;
        if (rnd() % 50 == 0) time = (rnd() & 1) ? 0.0f : S2S_NO_TIME;
        char name[4] = { (char)('A' + rnd() % 26), (char)('A' + rnd() % 26), (char)('A' + rnd() % 26), 0 };
        int team = (int)(rnd() % 4) + 1, car = (int)(rnd() % 5) + 1, replay = (int)(rnd() % 100);
        int got = s2save_add_time(kind, track, list, time, name, team, car, replay);
        int want = model_add(kind, track, list, time, name, team, car, replay);
        if (got != want) { CHECK(0, "insertion %d: rank %d, model %d", i, got, want); break; }
        if (i % 997 == 0 && !model_same("random")) break;
    }
    model_same("random end");
    check_sealed("random end");
}

/* every key, name, set-up, car5 and some times changed to something that is not the default */
static void make_good(void)
{
    power_on(file, 0);
    for (int k = 0; k < S2S_KEYS; k++) s2save_set(k, key_hi[k] - (k % 2));
    s2save_set_name("ZED");
    for (int t = 1; t <= 4; t++) {
        s2save_set_car5(t, t & 1);
        for (int c = 1; c <= 4; c++)
            for (int s = 0; s < 4; s++) {
                int8_t six[6];
                for (int i = 0; i < 6; i++) six[i] = (int8_t)((t + c + s + i) % 6 - 1);
                s2save_set_setup(t, c, s, six);
            }
    }
    rng_state = 99;
    for (int i = 0; i < 4000; i++)
        s2save_add_time((int)(rnd() % 3), (int)(rnd() % 7) + 1, (int)(rnd() % 5), (float)(rnd() % 3000) / 10.0f + 20.0f, "ABC", (int)(rnd() % 4) + 1, (int)(rnd() % 5) + 1, (int)(rnd() % 20));
    memcpy(good, win, S2SAVE_SIZE);
}

static void test_round_trip(void)
{
    make_good();
    check_sealed("good");
    CHECK(strcmp(s2save_name(), "MIL") && s2save_get(S2S_LAPS) != key_def[S2S_LAPS], "the good save is not the defaults");
    power_on(good, S2SAVE_SIZE);
    CHECK(has("loaded (19348 bytes)"), "log '%s'", log_text);
    CHECK(!memcmp(win, good, S2SAVE_SIZE), "the file read back is the file written");
    check_sealed("round trip");
    CHECK(set_size_calls == 1, "the slot size entered once");
}

static void old_block(uint8_t *out, int track, int team, int car, int laps, int skill, int manual, int marker)
{
    choice_defaults();
    choice.track = track;
    choice.intro = team;
    choice.car = car;
    choice.laps = laps;
    choice.difficulty = skill;
    choice.failed = marker ? (int32_t)(0x53320000u | (uint32_t)(manual != 0)) : 0;
    choice.volume[0] = 0x1234;
    choice.view = 3;
    memcpy(out, &choice, CHOICE_SIZE);
}

static void test_migration(void)
{
    uint8_t old[CHOICE_SIZE];
    CHECK(CHOICE_SIZE == 1120, "the old file is 1120 bytes");
    for (int manual = 0; manual < 2; manual++) {
        old_block(old, 5, 2, 4, 2, 2, manual, 1);               /* track 6, team 3, car 5, 5 laps, PRO */
        power_on(old, CHOICE_SIZE);
        check_sealed("migrated");
        CHECK(has("old layout, choices kept (1120 bytes)"), "log '%s'", log_text);
        CHECK(s2save_get(S2S_TRACK) == 6 && s2save_get(S2S_TEAM) == 3 && s2save_get(S2S_CAR) == 5 && s2save_get(S2S_LAPS) == 2 && s2save_get(S2S_SKILL) == 2 &&
              s2save_get(S2S_TRANS) == manual, "choices carried over: %d %d %d %d %d %d", s2save_get(S2S_TRACK), s2save_get(S2S_TEAM), s2save_get(S2S_CAR), s2save_get(S2S_LAPS),
              s2save_get(S2S_SKILL), s2save_get(S2S_TRANS));
        CHECK(s2save_get(S2S_VOL_FX) == 0x6000 && s2save_get(S2S_VIEW) == 2 && !strcmp(s2save_name(), "MIL"), "the rest is the defaults");
        expect_defaults_times("migrated");
        CHECK(!memcmp(win, "S2SV", 4), "now the new layout");
        /* the file the Pocket writes now is the new layout, read as such next time: the old marker is not needed again */
        uint8_t now[S2SAVE_SIZE];
        memcpy(now, win, S2SAVE_SIZE);
        power_on(now, S2SAVE_SIZE);
        CHECK(has("loaded"), "second start '%s'", log_text);
        CHECK(s2save_get(S2S_TRACK) == 6 && s2save_get(S2S_TEAM) == 3 && s2save_get(S2S_CAR) == 5 && s2save_get(S2S_TRANS) == manual, "kept after the second start");
    }
    /* the first start's values behind the marker */
    old_block(old, 0, 0, 0, 1, 1, 0, 1);
    power_on(old, CHOICE_SIZE);
    CHECK(s2save_get(S2S_TRACK) == 1 && s2save_get(S2S_TEAM) == 1 && s2save_get(S2S_CAR) == 1 && s2save_get(S2S_LAPS) == 1 && s2save_get(S2S_SKILL) == 1 && s2save_get(S2S_TRANS) == 0,
          "the start's values");
    /* no marker: a block of Screamer 1, or the game's defaults */
    old_block(old, 5, 2, 4, 2, 2, 1, 0);
    power_on(old, CHOICE_SIZE);
    check_sealed("no marker");
    CHECK(has("old layout without choices: defaults"), "log '%s'", log_text);
    expect_defaults_main("no marker");
    /* values out of range are clamped, as the start menu did */
    old_block(old, 99, 77, -4, 12, -1, 1, 1);
    power_on(old, CHOICE_SIZE);
    check_sealed("clamped");
    CHECK(s2save_get(S2S_TRACK) == 7 && s2save_get(S2S_TEAM) == 4 && s2save_get(S2S_CAR) == 1 && s2save_get(S2S_LAPS) == 3 && s2save_get(S2S_SKILL) == 0 && s2save_get(S2S_TRANS) == 1,
          "clamped: %d %d %d %d %d %d", s2save_get(S2S_TRACK), s2save_get(S2S_TEAM), s2save_get(S2S_CAR), s2save_get(S2S_LAPS), s2save_get(S2S_SKILL), s2save_get(S2S_TRANS));
    old_block(old, -100000, 0x7FFFFFFF, 0x7FFFFFFF, -0x7FFFFFFF - 1, 0x7FFFFFFF, 1, 1);
    power_on(old, CHOICE_SIZE);
    check_sealed("clamped wide");
    CHECK(s2save_get(S2S_TRACK) == 1 && s2save_get(S2S_TEAM) == 4 && s2save_get(S2S_CAR) == 5 && s2save_get(S2S_LAPS) == 0 && s2save_get(S2S_SKILL) == 2, "clamped wide");
    /* a 1,120-byte file of zeros or 0xFF: no marker */
    memset(old, 0, sizeof old);
    power_on(old, CHOICE_SIZE);
    check_sealed("zeros");
    expect_defaults_main("zeros");
    memset(old, 0xFF, sizeof old);
    power_on(old, CHOICE_SIZE);
    check_sealed("ones");
    CHECK(has("without choices"), "0xFF file: '%s'", log_text);
}

static void test_robustness(void)
{
    make_good();
    static uint8_t buf[1 << 20];

    /* empty, short */
    for (uint32_t n = 0; n < 4; n++) {
        power_on(good, n);
        check_sealed("tiny");
        CHECK(n == 0 ? has("none: defaults") : has("too short: defaults"), "size %u: '%s'", n, log_text);
        CHECK(read_calls == 0, "a file under 4 bytes is not read");
        expect_defaults_main("tiny");
        expect_defaults_times("tiny");
    }
    /* the good file cut at many sizes: the main part survives once it is whole, the times once the file is */
    static const uint32_t sizes[] = { 4, 5, 8, 12, 16, 20, 100, 431, 447, 448, 449, 1000, 1119, 1120, 1121, 5000, 19344, 19345, 19347, 19348 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t n = sizes[i];
        power_on(good, n);
        check_sealed("cut");
        int main_whole = (n & ~3u) >= S2SAVE_TIMES_OFF, times_whole = (n & ~3u) >= S2SAVE_SIZE;
        if (main_whole) CHECK(s2save_get(S2S_TRACK) == key_hi[S2S_TRACK] - (S2S_TRACK % 2) && !strcmp(s2save_name(), "ZED"), "size %u keeps the main part", n);
        else expect_defaults_main("cut");
        if (times_whole) CHECK(!memcmp(win + S2SAVE_TIMES_OFF, good + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE), "size %u keeps the times", n);
        else expect_defaults_times("cut");
    }

    /* foreign: random bytes of several sizes (the game's own 33,244 among them), all zeros, all 0xFF, text */
    static const uint32_t foreign[] = { 19348, 33244, 20000, 19347, 36864, 65536, 100000, 1 << 20 };
    for (unsigned i = 0; i < sizeof foreign / sizeof foreign[0]; i++) {
        for (uint32_t k = 0; k < foreign[i]; k++) buf[k] = (uint8_t)rnd();
        power_on(buf, foreign[i]);
        check_sealed("foreign random");
        CHECK(has("not ours: defaults"), "random %u bytes: '%s'", foreign[i], log_text);
        expect_defaults_main("foreign");
        expect_defaults_times("foreign");
    }
    for (int fill = 0; fill < 2; fill++) {
        memset(buf, fill ? 0xFF : 0, 40000);
        power_on(buf, 33244);
        check_sealed("foreign fill");
        expect_defaults_main("foreign fill");
        expect_defaults_times("foreign fill");
    }
    memset(buf, 0, 40000);
    memcpy(buf, "this is not a save file at all, it is text", 42);
    power_on(buf, 33244);
    check_sealed("text");
    expect_defaults_main("text");

    /* oversized: a good file followed by more; nothing but the layout is read, and nothing past it is written */
    memcpy(buf, good, S2SAVE_SIZE);
    for (uint32_t k = S2SAVE_SIZE; k < 100000; k++) buf[k] = (uint8_t)rnd();
    power_on(buf, 100000);
    check_sealed("oversized");
    CHECK(has("loaded") && !memcmp(win, good, S2SAVE_SIZE), "an oversized file with a good start is good: '%s'", log_text);
    power_on(buf, 36864);
    check_sealed("36864");
    CHECK(has("loaded") && !memcmp(win, good, S2SAVE_SIZE), "36864 bytes");
    power_on(buf, 1 << 20);
    check_sealed("1 MB");
    static const uint32_t absurd[] = { 0x7FFFFFFFu, 0xFFFFFFFFu, 0x80000000u };          /* sizes from the slot table that cannot be right: the read is capped */
    for (unsigned i = 0; i < 3; i++) {
        file_size = absurd[i];
        memset(win, 0xCC, S2SAVE_SIZE);
        log_text[0] = 0;
        set_size = 0;
        set_size_calls = 0;
        s2save_load();
        check_sealed("absurd");
        CHECK(has("loaded") && !memcmp(win, good, S2SAVE_SIZE), "the absurd size %u: '%s'", absurd[i], log_text);
    }

    /* a read that fails */
    read_fails = 1;
    power_on(good, S2SAVE_SIZE);
    read_fails = 0;
    check_sealed("read fails");
    CHECK(has("unreadable: defaults"), "log '%s'", log_text);
    expect_defaults_main("read fails");
    expect_defaults_times("read fails");

    /* wrong header */
    memcpy(buf, good, S2SAVE_SIZE);
    buf[4] ^= 2;                                            /* version 3 */
    power_on(buf, S2SAVE_SIZE);
    check_sealed("version");
    CHECK(has("other version: defaults"), "log '%s'", log_text);
    expect_defaults_main("version");
    expect_defaults_times("version");
    memcpy(buf, good, S2SAVE_SIZE);
    buf[6] ^= 1;                                            /* size 19349 */
    power_on(buf, S2SAVE_SIZE);
    check_sealed("size field");
    CHECK(has("other version: defaults"), "log '%s'", log_text);
    memcpy(buf, good, S2SAVE_SIZE);
    wr16(buf + 4, 2);                                       /* the next version: not read by this one */
    power_on(buf, S2SAVE_SIZE);
    check_sealed("v2");
    expect_defaults_main("v2");

    /* damage with checksums that match: values out of range, a time that is not one, a name with a control character */
    struct { uint32_t at; uint8_t v; int in_main; const char *what; } bad[] = {
        { 20 + 2, 9, 1, "laps 9" }, { 20 + 1, 3, 1, "detail 3" }, { 34, 0, 1, "view 0" }, { 34, 8, 1, "view 8" }, { 35, 0, 1, "track 0" }, { 35, 8, 1, "track 8" },
        { 36, 5, 1, "team 5" }, { 37, 6, 1, "car 6" }, { 38, 4, 1, "league 4" }, { 39, 5, 1, "tracks open 5" }, { 29, 0x80, 1, "volume 0x80xx" },
        { 16, 7, 1, "name with a control character" }, { 19, 'X', 1, "name without its 0" }, { 48, 2, 1, "bonus car 2" }, { 49, 5, 1, "set-up value 5" },
        { 49, 0xFE, 1, "set-up value -2" },
        { 448 + 3, 0xC1, 0, "a negative time" }, { 448 + 3, 0x7F, 0, "a huge time" }, { 448 + 4, 1, 0, "name with a control character in a row" },
        { 448 + 8, 0, 0, "team 0 in a row" }, { 448 + 9, 6, 0, "car 6 in a row" }, { 448 + 180 + 120 + 8, 9, 0, "team 9 in a row" },
        { 448 + 180 * 104 + 12 * 14 + 3, 0x80, 0, "a negative time in the last row" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memcpy(buf, good, S2SAVE_SIZE);
        buf[bad[i].at] = bad[i].v;
        wr32(buf + 8, crc(buf + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE));
        wr32(buf + 12, crc(buf + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE));
        power_on(buf, S2SAVE_SIZE);
        check_sealed(bad[i].what);
        if (bad[i].in_main) {
            expect_defaults_main(bad[i].what);
            CHECK(!memcmp(win + S2SAVE_TIMES_OFF, good + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE), "%s: the times are kept", bad[i].what);
        } else {
            expect_defaults_times(bad[i].what);
            CHECK(!memcmp(win + S2SAVE_MAIN_OFF, good + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE), "%s: the main part is kept", bad[i].what);
        }
        CHECK(has("defaults for"), "%s: log '%s'", bad[i].what, log_text);
    }
    {   /* not-a-number, infinite and too large times */
        const uint32_t cases[3] = { 0x7FC00000u, 0x7F800000u, 0x4560FFFDu };
        for (int i = 0; i < 3; i++) {
            memcpy(buf, good, S2SAVE_SIZE);
            wr32(buf + 448 + 12 * 3, cases[i]);
            wr32(buf + 12, crc(buf + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE));
            power_on(buf, S2SAVE_SIZE);
            check_sealed("nan");
            expect_defaults_times("nan");
        }
    }

    /* every byte of the good file damaged in turn (two ways): the damaged part is the defaults, the other part is as it was, and
       the header fields that every part needs make everything the defaults */
    int main_defaults = 0, times_defaults = 0, all_defaults = 0, kept = 0;
    for (uint32_t at = 0; at < S2SAVE_SIZE; at++) {
        for (int how = 0; how < 2; how++) {
            memcpy(buf, good, S2SAVE_SIZE);
            buf[at] ^= how ? 0x01 : 0xFF;
            power_on(buf, S2SAVE_SIZE);
            int main_same = !memcmp(win + S2SAVE_MAIN_OFF, good + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE);
            int times_same = !memcmp(win + S2SAVE_TIMES_OFF, good + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE);
            int ok;
            if (at < 8) ok = !main_same && !times_same;                 /* marker, version, size: nothing is read */
            else if (at < 12) ok = !main_same && times_same;            /* crc_main */
            else if (at < 16) ok = main_same && !times_same;            /* crc_times */
            else if (at < S2SAVE_TIMES_OFF) ok = !main_same && times_same;
            else ok = main_same && !times_same;
            if (!ok || !sealed_quick() || log_text[0] == 0) {
                CHECK(0, "byte %u damaged (%s): main %s, times %s, sealed %d, log '%s'", at, how ? "^1" : "^FF", main_same ? "kept" : "defaults",
                      times_same ? "kept" : "defaults", sealed_quick(), log_text);
                at = S2SAVE_SIZE;
                break;
            }
            if (at < 8) all_defaults++; else if (at < S2SAVE_TIMES_OFF && at != 12 && at != 13 && at != 14 && at != 15) main_defaults++; else times_defaults++;
            kept++;
            if (at % 509 == 0) check_sealed("flip");
        }
    }
    CHECK(kept == 2 * S2SAVE_SIZE, "all single-byte damages handled (%d)", kept);
    printf("  every byte damaged in two ways: %d times the main part fell back to the defaults, %d times the times, %d times both\n", main_defaults, times_defaults, all_defaults);

    /* random damage: several bytes, a random cut, a random size reported; only the invariants */
    for (int i = 0; i < 3000; i++) {
        uint32_t n = S2SAVE_SIZE, damage = rnd() % 8;
        memcpy(buf, good, S2SAVE_SIZE);
        for (uint32_t d = 0; d < damage; d++) buf[rnd() % S2SAVE_SIZE] = (uint8_t)rnd();
        if (rnd() % 3 == 0) n = rnd() % (S2SAVE_SIZE + 100);
        if (rnd() % 7 == 0) {                               /* a stretch of random bytes at a random place */
            uint32_t at = rnd() % S2SAVE_SIZE, len = rnd() % 200;
            for (uint32_t k = at; k < at + len && k < S2SAVE_SIZE; k++) buf[k] = (uint8_t)rnd();
        }
        if (rnd() % 11 == 0) {                              /* the checksums made to match the damage */
            wr32(buf + 8, crc(buf + S2SAVE_MAIN_OFF, S2SAVE_MAIN_SIZE));
            wr32(buf + 12, crc(buf + S2SAVE_TIMES_OFF, S2SAVE_TIMES_SIZE));
        }
        if (rnd() % 5 == 0) { wr32(buf, 0x56533253u); wr16(buf + 4, 1); wr16(buf + 6, S2SAVE_SIZE); }
        power_on(buf, n);
        check_sealed("random damage");
        if (failures) { printf("  (random damage round %d, size %u)\n", i, n); break; }
    }

    /* writing after a damaged start works and is sealed */
    power_on(buf, 0);
    s2save_set(S2S_LAPS, 3);
    s2save_add_time(S2S_KIND_ARCADE, 1, 1, 77.0f, "NEW", 1, 1, 0);
    check_sealed("after");
}

int main(void)
{
    void *m = mmap((void *)(uintptr_t)SAVE_CHOICE_ADDR, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (m == MAP_FAILED || m != (void *)(uintptr_t)SAVE_CHOICE_ADDR) {
        perror("mmap of the CHOICE.DAT window");
        return 2;
    }
    memset(win + S2SAVE_SIZE, 0x5A, 0x10000 - S2SAVE_SIZE);        /* the rest of CHOICE.DAT's 64 KB, and ATTACK.DAT's window: sentinels */
    memset(guard, 0x5A, GUARD_BYTES);
    typedef void (*test_fn)(void);
    static const struct { const char *name; test_fn fn; } tests[] = {
        { "defaults and layout", test_defaults }, { "interface", test_api }, { "best times", test_times }, { "round trip", test_round_trip },
        { "migration of the old layout", test_migration }, { "robustness", test_robustness },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures, c0 = checks;
        s2save_unlock_all = S2SAVE_UNLOCK_ALL;
        tests[i].fn();
        printf("%-30s %s (%d checks)\n", tests[i].name, failures == before ? "ok" : "FAILED", checks - c0);
        if (failures) break;
    }
    printf(failures ? "save: %d of %d checks FAILED\n" : "save: all %d checks passed\n", failures ? failures : checks, checks);
    return failures != 0;
}
