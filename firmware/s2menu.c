/*
 * The start menu (s2menu.h)
 */
#include "s2menu.h"
#include "hw.h"
#include "console.h"
#include "dlog.h"
#include "screen.h"
#include "zip.h"
#include "dat.h"
#include "s2save.h"
#include "s2pages.h"
#include <string.h>

const uint8_t s2_menu_laps[S2M_LAPS] = { 1, 3, 5, 10 };

/* ------------------------------------------------------------------------------------------------ the texts */

/* STRINGS.TXT's lines that the menu shows: the names of the original (not to be translated for the tracks, teams and levels) */
static const char *track_name[S2M_TRACKS] = { "ENGLAND", "COLUMBIA", "EGYPT", "CALIFORNIA", "FINLAND", "SWITZERLAND", "THE TUNNEL" };
static const char *team_name[S2M_TEAMS] = { "CONDOR", "ANGELS", "WASP", "ZEUS" };
static const char *skill_name[S2M_SKILLS] = { "ROOKIE", "NORMAL", "PRO" };
static const char *lab_track = "TRACK SELECTION", *lab_team = "TEAM SELECTION", *lab_car = "CAR SELECTION";
static const char *lab_laps = "NUMBER OF LAPS", *lab_skill = "ARCADE ENEMIES SKILL", *lab_gear = "TRANSMISSION";
static const char *gear_name[2] = { "AUTOMATIC", "MANUAL" };
static const char *title = "ARCADE";

#define LOCAL_MAX   0x2000u             /* LOCAL.DAT is 2.7 KB stored */
#define TEXT_MAX    0xC00u              /* STRINGS.TXT is 2.6 KB */
#define LINES_MAX   320                 /* and about 260 lines */
#define S2M_NAME_MAX    24                  /* characters of a line that are kept */
static uint32_t local_buf[LOCAL_MAX / 4];
static char text[TEXT_MAX + 4];
static const char *line[LINES_MAX];

static int find_line(int n, const char *s)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(line[i], s)) return i;
    return -1;
}

int s2menu_init(struct zip *z)
{
    int found = 0;
    const struct zip_entry *e = zip_find(z, "DATA/LOCAL.DAT");
    struct dat_entry d;
    if (!e || e->size + ZIP_SLACK > LOCAL_MAX || zip_read(z, e, (uint8_t *)local_buf, LOCAL_MAX)) return 0;
    if (!dat_find((const uint8_t *)local_buf, e->size, "STRINGS.TXT", &d)) return 0;
    uint32_t n = dat_extract((const uint8_t *)local_buf, e->size, &d, (uint8_t *)text, TEXT_MAX);
    if (!n) return 0;
    text[n] = 0;
    /* the lines: '\n' ends one, a trailing '\r' or space goes; the comments (*) and the empty lines are not kept */
    int lines = 0;
    for (char *p = text; *p && lines < LINES_MAX;) {
        char *s = p;
        while (*p && *p != '\n') p++;
        char *end = p;
        if (*p) p++;
        while (end > s && (end[-1] == '\r' || end[-1] == ' ')) end--;
        if (end - s > S2M_NAME_MAX) end = s + S2M_NAME_MAX;     /* (the buffers of the drawing are sized for it) */
        *end = 0;
        if (*s && *s != '*') line[lines++] = s;
    }
    int i = find_line(lines, "ENGLAND");                    /* the seven tracks follow the laps' labels, in order */
    if (i >= 0 && i + S2M_TRACKS <= lines && !strcmp(line[i + 6], "THE TUNNEL")) {
        for (int k = 0; k < S2M_TRACKS; k++) track_name[k] = line[i + k];
        found++;
    }
    i = find_line(lines, "CONDOR");                         /* the teams' names are not translated */
    if (i >= 0 && i + S2M_TEAMS <= lines && !strcmp(line[i + 3], "ZEUS")) {
        for (int k = 0; k < S2M_TEAMS; k++) team_name[k] = line[i + k];
        found++;
    }
    i = find_line(lines, ":ROOKIE");                        /* the levels: ":ROOKIE", ":NORMAL", ":PRO" */
    if (i >= 0 && i + S2M_SKILLS <= lines && !strcmp(line[i + 2], ":PRO")) {
        for (int k = 0; k < S2M_SKILLS; k++) skill_name[k] = line[i + k] + 1;
        found++;
    }
    static const struct { const char **dst; const char *key; } lab[] = {
        { &lab_track, "TRACK SELECTION" }, { &lab_team, "TEAM SELECTION" }, { &lab_car, "CAR SELECTION" },
        { &lab_laps, "NUMBER OF LAPS" }, { &lab_skill, "ARCADE ENEMIES SKILL" }, { &lab_gear, "TRANSMISSION" },
        { &gear_name[0], "AUTOMATIC" }, { &gear_name[1], "MANUAL" }, { &title, "ARCADE" },
    };
    for (unsigned k = 0; k < sizeof lab / sizeof lab[0]; k++) {
        i = find_line(lines, lab[k].key);
        if (i >= 0) {
            *lab[k].dst = line[i];
            found++;
        }
    }
    return found;
}

const char *s2menu_track_name(int track) { return track_name[track - 1]; }
const char *s2menu_team_name(int team) { return team_name[team - 1]; }
const char *s2menu_skill_name(int skill) { return skill_name[skill]; }

/* ------------------------------------------------------------------------------------------------ the choices */

void s2menu_defaults(struct s2_menu_cfg *c)
{
    *c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1, .manual = 0 };
}

/* The choices are kept in the save (s2save.h): the track, team and car are its start-menu fields; the laps, the level and the gearbox
   are the GAME SETTINGS rows of the same name. The save clamps what it is given and answers within the ranges. */
void s2menu_load(struct s2_menu_cfg *c)
{
    c->track = s2save_get(S2S_TRACK);
    c->team = s2save_get(S2S_TEAM);
    c->car = s2save_get(S2S_CAR);
    c->laps = s2save_get(S2S_LAPS);
    c->skill = s2save_get(S2S_SKILL);
    c->manual = s2save_get(S2S_TRANS);
}

void s2menu_store(const struct s2_menu_cfg *c)
{
    s2save_set(S2S_TRACK, c->track);
    s2save_set(S2S_TEAM, c->team);
    s2save_set(S2S_CAR, c->car);
    s2save_set(S2S_LAPS, c->laps);
    s2save_set(S2S_SKILL, c->skill);
    s2save_set(S2S_TRANS, c->manual != 0);
}

void s2menu_grid(const struct s2_menu_cfg *c, struct s2_pick grid[4])
{
    grid[0] = (struct s2_pick){ (uint8_t)c->team, (uint8_t)c->car };
    int k = 1;
    for (int t = 1; t <= S2M_TEAMS && k < 4; t++)
        if (t != c->team) grid[k++] = (struct s2_pick){ (uint8_t)t, (uint8_t)t };
}

/* ------------------------------------------------------------------------------------------------ the keys */

static int ring(int v, int d, int first, int count)
{
    return (v - first + d + count * 4) % count + first;
}

int s2menu_key(struct s2_menu_cfg *c, int *cursor, uint32_t press)
{
    if (press & KEY_UP) *cursor = ring(*cursor, -1, 0, S2M_ROWS);
    if (press & KEY_DOWN) *cursor = ring(*cursor, 1, 0, S2M_ROWS);
    if (press & (KEY_L | KEY_R)) c->track = ring(c->track, (press & KEY_R) ? 3 : -3, 1, S2M_TRACKS);
    int d = ((press & KEY_RIGHT) ? 1 : 0) - ((press & KEY_LEFT) ? 1 : 0);
    if (d) switch (*cursor) {
    case S2M_ROW_TRACK: c->track = ring(c->track, d, 1, S2M_TRACKS); break;
    case S2M_ROW_TEAM: c->team = ring(c->team, d, 1, S2M_TEAMS); break;
    case S2M_ROW_CAR: c->car = ring(c->car, d, 1, S2M_CARS); break;
    case S2M_ROW_LAPS: c->laps = ring(c->laps, d, 0, S2M_LAPS); break;
    case S2M_ROW_SKILL: c->skill = ring(c->skill, d, 0, S2M_SKILLS); break;
    case S2M_ROW_GEAR: c->manual = !c->manual; break;
    default: break;
    }
    if (press & KEY_X) return S2M_WATCH;
    if (press & (KEY_A | KEY_START))
        return *cursor == S2M_ROW_WATCH ? S2M_WATCH : *cursor == S2M_ROW_SETTINGS ? S2M_SETTINGS : *cursor == S2M_ROW_SCORES ? S2M_SCORES : S2M_RACE;
    return S2M_NONE;
}

/* ------------------------------------------------------------------------------------------------ the picture */

/* the palette: black-blue, white, green, red, grey, the cursor's bar, yellow (the values), cyan */
enum { BG, WHITE, GREEN, RED, GREY, BAR, YELLOW, CYAN, PAL_N };
static const uint32_t pal[PAL_N] = { 0x101018, 0xFFFFFF, 0x40D070, 0xF04848, 0x909090, 0x2050B0, 0xF0D040, 0x50D8F0 };

#define VALUE_COL   25                  /* the values start here, their arrows on either side */
#define LAST_COL    38                  /* the bar covers columns 1 .. 38 */

/* a row: the label from column 2, the value from VALUE_COL (adjustable: with arrows at the cursor) */
static void row(volatile uint8_t *fb, int r, int sel, const char *label, uint8_t label_col, const char *value, int adjust)
{
    static const char blank[LAST_COL + 1] = "                                      ";
    const uint8_t bg = sel ? BAR : BG;
    con_text(fb, 1, r, blank, WHITE, bg);
    con_text(fb, 2, r, label, sel ? WHITE : label_col, bg);
    if (value) con_text(fb, VALUE_COL, r, value, YELLOW, bg);
    if (sel && adjust) {
        con_text(fb, VALUE_COL - 2, r, "<", CYAN, bg);
        con_text(fb, LAST_COL, r, ">", CYAN, bg);
    }
}

void s2menu_draw(volatile uint8_t *fb, const struct s2_menu_cfg *c, int cursor)
{
    char v[64], o[128], *p;
    for (int i = 0; i < PAL_N; i++) VID_PAL[i] = pal[i];
    fb_fill(fb, 0, 0, FB_W, FB_H, BG);
    fb_fill(fb, 0, 0, FB_W, 8, WHITE);
    fmt_str(fmt_str(v, "SCREAMER 2 - "), title);
    con_text(fb, 1, 0, v, BG, WHITE);

    fmt_str(fmt_str(fmt_udec(v, (uint32_t)c->track), " "), track_name[c->track - 1]);
    row(fb, 2, cursor == S2M_ROW_TRACK, lab_track, GREY, v, 1);
    row(fb, 3, cursor == S2M_ROW_TEAM, lab_team, GREY, team_name[c->team - 1], 1);
    p = fmt_udec(v, (uint32_t)c->car);
    if (c->car == S2M_CARS) fmt_str(p, " BONUS");
    row(fb, 4, cursor == S2M_ROW_CAR, lab_car, GREY, v, 1);
    fmt_udec(v, s2_menu_laps[c->laps]);
    row(fb, 5, cursor == S2M_ROW_LAPS, lab_laps, GREY, v, 1);
    row(fb, 6, cursor == S2M_ROW_SKILL, lab_skill, GREY, skill_name[c->skill], 1);
    row(fb, 7, cursor == S2M_ROW_GEAR, lab_gear, GREY, gear_name[c->manual != 0], 1);

    row(fb, 9, cursor == S2M_ROW_RACE, "START RACE", GREEN, 0, 0);
    row(fb, 10, cursor == S2M_ROW_WATCH, "WATCH", CYAN, 0, 0);
    con_text(fb, 12, 10, "the ride round the track", GREY, cursor == S2M_ROW_WATCH ? BAR : BG);
    row(fb, 11, cursor == S2M_ROW_SETTINGS, "GAME SETTINGS", CYAN, 0, 0);
    row(fb, 12, cursor == S2M_ROW_SCORES, "HI SCORES", CYAN, 0, 0);

    struct s2_pick g[4];
    s2menu_grid(c, g);
    fmt_udec(fmt_str(fmt_str(v, team_name[g[0].team - 1]), " "), g[0].car);
    con_text(fb, 2, 14, "YOU", GREY, BG);
    con_text(fb, 12, 14, v, WHITE, BG);
    p = o;
    *p = 0;
    for (int k = 1; k < 4; k++) {
        if (k > 1) p = fmt_str(p, "  ");
        p = fmt_udec(fmt_str(fmt_str(p, team_name[g[k].team - 1]), " "), g[k].car);
    }
    con_text(fb, 2, 15, "AGAINST", GREY, BG);
    con_text(fb, 12, 15, o, WHITE, BG);

    con_text(fb, 2, 16, "d-pad: up/down row, left/right value", GREY, BG);
    con_text(fb, 2, 17, "L / R: track -3 / +3 from any row", GREY, BG);
    con_text(fb, 2, 18, "A or Start: race   X: watch", GREY, BG);
    con_text(fb, 2, 20, "in the race or the ride:", WHITE, BG);
    static const char *const keys[4][2] = { { "Select+B", "back to this menu" }, { "Select+Start", "race again" },
                                            { "Select+left/right", "other track" }, { "Start", "ride / drive" } };
    for (int k = 0; k < 4; k++) {
        con_text(fb, 3, 21 + k, keys[k][0], GREY, BG);
        con_text(fb, 22, 21 + k, keys[k][1], GREY, BG);
    }
}

void s2menu_log(const struct s2_menu_cfg *c, int go)
{
    char l[96], *p = fmt_udec(fmt_str(l, go == S2M_RACE ? "menu: race T" : "menu: watch T"), (uint32_t)c->track);
    p = fmt_udec(fmt_str(fmt_str(p, " "), team_name[c->team - 1]), (uint32_t)c->car);
    p = fmt_str(fmt_udec(fmt_str(p, " "), s2_menu_laps[c->laps]), "L ");
    fmt_str(fmt_str(fmt_str(p, skill_name[c->skill]), c->manual ? " MAN" : " AUTO"), "\n");
    dlog_str(l);
}

#define REPEAT_FIRST    25              /* a held d-pad button repeats after 0.5 s, every 6th frame */
#define REPEAT_EVERY    6

int s2menu_run(struct s2_menu_cfg *c)
{
    int cursor = S2M_ROW_TRACK;
    uint32_t keys_before = in_keys(), held = 0;     /* (the button that left the files screen is still down) */
    const uint32_t dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
    for (;;) {
        uint32_t st = wait_swap_done();
        uint32_t keys = in_keys(), press = keys & ~keys_before;
        held = (keys & dirs) && keys == keys_before ? held + 1 : 0;
        if (held >= REPEAT_FIRST && (held - REPEAT_FIRST) % REPEAT_EVERY == 0) press |= keys & dirs;
        keys_before = keys;
        int r = s2menu_key(c, &cursor, press);
        if (r == S2M_SETTINGS || r == S2M_SCORES) {     /* a page of GAME SETTINGS / HI SCORES (s2pages.h): the choices go in, and come back as it left them */
            s2menu_store(c);
            if (r == S2M_SETTINGS) s2_page_settings();
            else s2_page_hiscores();
            s2menu_load(c);
            keys_before = in_keys();
            held = 0;
            continue;
        }
        if (r) {
            s2menu_store(c);
            return r;
        }
        s2menu_draw(FB(!(st & VID_FRONT)), c, cursor);
        VID_SWAP = 1;
    }
}
