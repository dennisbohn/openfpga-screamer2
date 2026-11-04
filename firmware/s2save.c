/*
 * The save of Screamer 2 (s2save.h)
 */
#include "s2save.h"
#include "save.h"
#include "champ.h"
#include "crc32.h"
#include "slot.h"
#include "dlog.h"
#include <stddef.h>
#include <string.h>

#define MAGIC   0x56533253u                 /* "S2SV" */

int s2save_unlock_all = S2SAVE_UNLOCK_ALL;

/* ------------------------------------------------------------------------------------------------ the layout */

struct head {
    uint32_t magic;
    uint16_t version, size;                 /* size: S2SAVE_SIZE */
    uint32_t crc_main, crc_times;           /* CRC-32 of the main part, of the times */
};

struct team_rec {
    uint8_t car5;                           /* the fifth car won */
    int8_t setup[4][4][6];                  /* [car 1 .. 4][Custom 1 .. 4][the six numbers] */
    uint8_t pad[3];
};

struct main_part {
    char name[4];
    uint8_t set[8];                         /* the GAME SETTINGS rows */
    uint16_t vol[3];                        /* FX, CD, FM */
    uint8_t view, track, team, car, league_open, tracks_open;
    uint8_t reserved[8];                    /* 0; a later field takes one of them (its 0 must be a valid value or clamp to one) */
    struct team_rec team_rec[4];
};

struct file {
    struct head h;
    struct main_part m;
    struct s2save_row t[S2S_KINDS][S2S_TRACKS][S2S_LISTS][S2S_ROWS];
};

_Static_assert(sizeof(struct head) == S2SAVE_MAIN_OFF, "header");
_Static_assert(sizeof(struct main_part) == S2SAVE_MAIN_SIZE && offsetof(struct file, m) == S2SAVE_MAIN_OFF, "main part");
_Static_assert(sizeof(((struct file *)0)->t) == S2SAVE_TIMES_SIZE && offsetof(struct file, t) == S2SAVE_TIMES_OFF, "times");
_Static_assert(sizeof(struct file) == S2SAVE_SIZE && sizeof(struct s2save_row) == 12, "size");
_Static_assert(S2SAVE_SIZE <= SAVE_ATTACK_ADDR - SAVE_CHOICE_ADDR, "the window of the slot");

#define sv  ((struct file *)(uintptr_t)SAVE_CHOICE_ADDR)

/* ------------------------------------------------------------------------------------------------ the numbers */

static const struct key {
    uint8_t off, size;                      /* in the main part, bytes */
    int16_t lo, hi, def;
} keys[S2S_KEYS] = {
    [S2S_TRANS] = { offsetof(struct main_part, set[0]), 1, 0, 1, 0 },
    [S2S_DETAIL] = { offsetof(struct main_part, set[1]), 1, 0, 2, 2 },
    [S2S_LAPS] = { offsetof(struct main_part, set[2]), 1, 0, 3, 1 },
    [S2S_SKILL] = { offsetof(struct main_part, set[3]), 1, 0, 2, 1 },
    [S2S_MP_ENEMIES] = { offsetof(struct main_part, set[4]), 1, 0, 1, 0 },
    [S2S_GHOST] = { offsetof(struct main_part, set[5]), 1, 0, 1, 0 },
    [S2S_DAMAGE] = { offsetof(struct main_part, set[6]), 1, 0, 1, 0 },
    [S2S_MUSIC] = { offsetof(struct main_part, set[7]), 1, 0, 1, 0 },
    [S2S_VIEW] = { offsetof(struct main_part, view), 1, 1, 7, 2 },
    [S2S_VOL_FX] = { offsetof(struct main_part, vol[0]), 2, 0, 0x7FFF, 0x6000 },
    [S2S_VOL_CD] = { offsetof(struct main_part, vol[1]), 2, 0, 0x7FFF, 0x6000 },
    [S2S_VOL_FM] = { offsetof(struct main_part, vol[2]), 2, 0, 0x7FFF, 0x6000 },
    [S2S_TRACK] = { offsetof(struct main_part, track), 1, 1, 7, 1 },
    [S2S_TEAM] = { offsetof(struct main_part, team), 1, 1, 4, 1 },
    [S2S_CAR] = { offsetof(struct main_part, car), 1, 1, 5, 1 },
    [S2S_LEAGUE_OPEN] = { offsetof(struct main_part, league_open), 1, 0, 3, 0 },
    [S2S_TRACKS_OPEN] = { offsetof(struct main_part, tracks_open), 1, 0, 4, 0 },
};

static int raw(const struct main_part *m, int k)
{
    const uint8_t *p = (const uint8_t *)m + keys[k].off;
    return keys[k].size == 2 ? p[0] | p[1] << 8 : p[0];
}

static void put(struct main_part *m, int k, int v)
{
    uint8_t *p = (uint8_t *)m + keys[k].off;
    p[0] = (uint8_t)v;
    if (keys[k].size == 2) p[1] = (uint8_t)(v >> 8);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ------------------------------------------------------------------------------------------------ sealing */

static uint32_t crc_of(const void *p, uint32_t n)
{
    return crc32((const uint8_t *)p, (const uint8_t *)p + n);
}

static void seal_main(void)
{
    sv->h.crc_main = crc_of(&sv->m, sizeof sv->m);
}

static void seal_times(void)
{
    sv->h.crc_times = crc_of(sv->t, sizeof sv->t);
}

/* ------------------------------------------------------------------------------------------------ defaults and checks */

static void reset_main(void)
{
    struct main_part *m = &sv->m;
    memset(m, 0, sizeof *m);
    memcpy(m->name, "MIL", 4);
    for (int k = 0; k < S2S_KEYS; k++) put(m, k, keys[k].def);
    for (int t = 0; t < 4; t++)
        for (int c = 0; c < 4; c++)
            for (int s = 0; s < 4; s++)
                memcpy(m->team_rec[t].setup[c][s], (const int8_t[6]){ 2, 2, 0, 2, 2, 2 }, 6);
}

static void reset_times(void)
{
    struct s2save_row *r = &sv->t[0][0][0][0];
    for (int i = 0; i < S2S_KINDS * S2S_TRACKS * S2S_LISTS * S2S_ROWS; i++, r++)
        *r = (struct s2save_row){ S2S_NO_TIME, "MIL", (uint8_t)((i % S2S_ROWS & 3) + 1), 1, 0 };
}

void s2save_reset(void)
{
    reset_main();
    reset_times();
    sv->h = (struct head){ MAGIC, S2SAVE_VERSION, S2SAVE_SIZE, 0, 0 };
    seal_main();
    seal_times();
}

static int name_ok(const char *n)
{
    return n[0] >= ' ' && n[0] <= '~' && n[1] >= ' ' && n[1] <= '~' && n[2] >= ' ' && n[2] <= '~' && n[3] == 0;
}

static int main_ok(const struct main_part *m)
{
    if (!name_ok(m->name)) return 0;
    for (int k = 0; k < S2S_KEYS; k++)
        if (raw(m, k) < keys[k].lo || raw(m, k) > keys[k].hi) return 0;
    for (int t = 0; t < 4; t++) {
        if (m->team_rec[t].car5 > 1) return 0;
        for (int i = 0; i < 96; i++)
            if ((&m->team_rec[t].setup[0][0][0])[i] < -1 || (&m->team_rec[t].setup[0][0][0])[i] > 4) return 0;
    }
    return 1;
}

static int times_ok(void)
{
    const struct s2save_row *r = &sv->t[0][0][0][0];
    for (int i = 0; i < S2S_KINDS * S2S_TRACKS * S2S_LISTS * S2S_ROWS; i++, r++)
        if (!(r->time >= 0.0f && r->time <= S2S_NO_TIME) || !name_ok(r->name) || r->team < 1 || r->team > 4 || r->car < 1 || r->car > 5)
            return 0;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ reading the slot */

#define OLD_MAGIC   0x53320000u             /* the old layout's marker in choice.failed (the high half), its low bit the gearbox */

static uint32_t old32(uint32_t off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)(uintptr_t)SAVE_CHOICE_ADDR + off, 4);
    return v;
}

/* the old layout: Screamer 1's CHOICE.DAT block (champ.h) whose unused fields held the start menu's choices */
static const char *migrate(void)
{
    if ((old32(offsetof(struct choice_dat, failed)) & 0xFFFF0000u) != OLD_MAGIC) {
        s2save_reset();
        return "old layout without choices: defaults";
    }
    /* the old fields, the first three counted from 0 (the keys count them from 1) */
    const int32_t v[6] = { (int32_t)old32(offsetof(struct choice_dat, track)), (int32_t)old32(offsetof(struct choice_dat, intro)),
                           (int32_t)old32(offsetof(struct choice_dat, car)), (int32_t)old32(offsetof(struct choice_dat, laps)),
                           (int32_t)old32(offsetof(struct choice_dat, difficulty)), (int32_t)(old32(offsetof(struct choice_dat, failed)) & 1) };
    static const uint8_t to[6] = { S2S_TRACK, S2S_TEAM, S2S_CAR, S2S_LAPS, S2S_SKILL, S2S_TRANS };
    s2save_reset();
    for (int i = 0; i < 6; i++) {
        int first = i < 3;
        put(&sv->m, to[i], clamp(v[i], keys[to[i]].lo - first, keys[to[i]].hi - first) + first);
    }
    return "old layout, choices kept";
}

/* the new layout in the window, n bytes of it read (the rest zeros): each part on its own */
static const char *check(uint32_t n)
{
    if (sv->h.version != S2SAVE_VERSION || sv->h.size != S2SAVE_SIZE) {
        s2save_reset();
        return "other version: defaults";
    }
    int m = n >= S2SAVE_TIMES_OFF && crc_of(&sv->m, sizeof sv->m) == sv->h.crc_main && main_ok(&sv->m);
    int t = n >= S2SAVE_SIZE && crc_of(sv->t, sizeof sv->t) == sv->h.crc_times && times_ok();
    if (!m) reset_main();
    if (!t) reset_times();
    return m && t ? "loaded" : m ? "best times damaged or short: defaults for them" : t ? "main part damaged or short: defaults for it"
                                                                                      : "damaged or short: defaults";
}

void s2save_load(void)
{
    uint32_t size = slot_size(SAVE_CHOICE_SLOT), n = size & ~3u;
    const char *how;
    if (n > S2SAVE_SIZE) n = S2SAVE_SIZE;                   /* (a longer file: only the layout's part; the window ends at ATTACK.DAT) */
    if (!n) {
        s2save_reset();
        how = size ? "too short: defaults" : "none: defaults";
    } else if (slot_read(SAVE_CHOICE_SLOT, 0, SAVE_CHOICE_ADDR, n)) {
        s2save_reset();
        how = "unreadable: defaults";
    } else {
        memset((uint8_t *)sv + n, 0, S2SAVE_SIZE - n);      /* (written after the read, not before: the APF writes the SDRAM on its own) */
        if (sv->h.magic == MAGIC) how = check(n);
        else if (size == CHOICE_SIZE) how = migrate();
        else {
            s2save_reset();
            how = "not ours: defaults";
        }
    }
    sv->h = (struct head){ MAGIC, S2SAVE_VERSION, S2SAVE_SIZE, 0, 0 };
    seal_main();
    seal_times();
    slot_set_size(SAVE_CHOICE_SLOT, S2SAVE_SIZE);
    dlog_str("save: CHOICE.DAT ");
    dlog_str(how);
    dlog_str(" (");
    dlog_dec((int32_t)size);
    dlog_str(" bytes)\n");
}

/* ------------------------------------------------------------------------------------------------ settings */

int s2save_get(int key)
{
    if ((unsigned)key >= S2S_KEYS) return 0;
    return clamp(raw(&sv->m, key), keys[key].lo, keys[key].hi);
}

void s2save_set(int key, int value)
{
    if ((unsigned)key >= S2S_KEYS) return;
    value = clamp(value, keys[key].lo, keys[key].hi);
    if (raw(&sv->m, key) == value) return;
    put(&sv->m, key, value);
    seal_main();
}

int s2save_count(int key)
{
    return (unsigned)key < S2S_KEYS ? keys[key].hi - keys[key].lo + 1 : 0;
}

const char *s2save_name(void)
{
    return name_ok(sv->m.name) ? sv->m.name : "MIL";
}

/* three printable characters, padded with spaces; whatever else becomes '?' */
static void name3(char out[4], const char *n)
{
    int end = 0;
    for (int i = 0; i < 3; i++) {
        char c = n && !end ? n[i] : 0;
        if (!c) end = 1;
        out[i] = end ? ' ' : c < ' ' || c > '~' ? '?' : c;
    }
    out[3] = 0;
}

void s2save_set_name(const char *name)
{
    name3(sv->m.name, name);
    seal_main();
}

/* ------------------------------------------------------------------------------------------------ unlocks and set-ups */

static const uint8_t order[S2S_TRACKS] = { 1, 3, 4, 5, 6, 2, 7 };

int s2save_track_at(int pos)
{
    return pos >= 0 && pos < S2S_TRACKS ? order[pos] : 0;
}

int s2save_track_open(int track)
{
    for (int p = 0; p < S2S_TRACKS; p++)
        if (order[p] == track) return s2save_unlock_all || p < 3 + s2save_get(S2S_TRACKS_OPEN);
    return 0;
}

int s2save_league_open(int league)
{
    return league >= 0 && league < 4 && (s2save_unlock_all || league <= s2save_get(S2S_LEAGUE_OPEN));
}

int s2save_car5_open(int team)
{
    return team >= 1 && team <= 4 && (s2save_unlock_all || sv->m.team_rec[team - 1].car5 == 1);
}

void s2save_set_car5(int team, int won)
{
    if (team < 1 || team > 4) return;
    sv->m.team_rec[team - 1].car5 = won != 0;
    seal_main();
}

static int8_t *setup_at(int team, int car, int slot)
{
    if (team < 1 || team > 4 || car < 1 || car > 4 || slot < 0 || slot > 3) return 0;
    return sv->m.team_rec[team - 1].setup[car - 1][slot];
}

const int8_t *s2save_setup(int team, int car, int slot)
{
    return setup_at(team, car, slot);
}

int s2save_set_setup(int team, int car, int slot, const int8_t six[6])
{
    int8_t *p = setup_at(team, car, slot);
    if (!p) return 1;
    for (int i = 0; i < 6; i++) p[i] = (int8_t)clamp(six[i], -1, 4);
    seal_main();
    return 0;
}

/* ------------------------------------------------------------------------------------------------ best times */

/* the list's 15 rows; time attack's totals are the 3-lap list */
static struct s2save_row *list(int kind, int track, int which)
{
    if ((unsigned)kind >= S2S_KINDS || track < 1 || track > S2S_TRACKS || (unsigned)which >= S2S_LISTS) return 0;
    if (kind == S2S_KIND_ATTACK && which != S2S_LAP) which = 1;
    return sv->t[kind][track - 1][which];
}

const struct s2save_row *s2save_row(int kind, int track, int which, int row)
{
    struct s2save_row *l = list(kind, track, which);
    return l && (unsigned)row < S2S_ROWS ? &l[row] : 0;
}

int s2save_add_time(int kind, int track, int which, float time, const char *name, int team, int car, int replay)
{
    struct s2save_row *l = list(kind, track, which);
    int at = 0;
    if (!l || !(time > 0.0f && time < S2S_NO_TIME)) return -1;
    while (at < S2S_ROWS && l[at].time < time) at++;        /* (STARTL.EXE 0x354cf: fcomp, ja: the new time stops at the first it is not slower than) */
    if (at >= S2S_ROWS) return -1;
    memmove(&l[at + 1], &l[at], (S2S_ROWS - 1 - at) * sizeof *l);
    l[at].time = time;
    name3(l[at].name, name ? name : s2save_name());
    l[at].team = (uint8_t)clamp(team, 1, 4);
    l[at].car = (uint8_t)clamp(car, 1, 5);
    l[at].replay = (uint16_t)(replay < 0 || replay > 0xFFFF ? 0 : replay);
    seal_times();
    return at;
}

void s2save_set_replay(int kind, int track, int which, int row, int replay)
{
    struct s2save_row *l = list(kind, track, which);
    if (!l || (unsigned)row >= S2S_ROWS) return;
    l[row].replay = (uint16_t)(replay < 0 || replay > 0xFFFF ? 0 : replay);
    seal_times();
}
