/*
 * The menu (s2menu.h)
 */
#include "s2menu.h"
#include "console.h"
#include "dlog.h"
#include "zip.h"
#include "dat.h"
#include "s2save.h"
#include "s2pages.h"
#include "s2arcade.h"
#include "s2pages3d.h"
#include "s2room.h"
#include <string.h>

const uint8_t s2_menu_laps[S2M_LAPS] = { 1, 3, 5, 10 };

/* ------------------------------------------------------------------------------------------------ the texts */

/* STRINGS.TXT's lines that the menu shows: the names of the original (not to be translated for the tracks, teams and levels) */
static const char *track_name[S2M_TRACKS] = { "ENGLAND", "COLUMBIA", "EGYPT", "CALIFORNIA", "FINLAND", "SWITZERLAND", "THE TUNNEL" };
static const char *team_name[S2M_TEAMS] = { "CONDOR", "ANGELS", "WASP", "ZEUS" };
static const char *skill_name[S2M_SKILLS] = { "ROOKIE", "NORMAL", "PRO" };

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
    c->free_run = 0;                                        /* (time attack's choice is made every time) */
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

/* ------------------------------------------------------------------------------------------------ the menu */

void s2menu_log(const struct s2_menu_cfg *c)
{
    char l[96], *p = fmt_udec(fmt_str(l, "menu: race T"), (uint32_t)c->track);
    p = fmt_udec(fmt_str(fmt_str(p, " "), team_name[c->team - 1]), (uint32_t)c->car);
    p = fmt_str(fmt_udec(fmt_str(p, " "), s2_menu_laps[c->laps]), "L ");
    fmt_str(fmt_str(fmt_str(p, skill_name[c->skill]), c->manual ? " MAN" : " AUTO"), "\n");
    dlog_str(l);
}

int (*s2menu_room)(const struct s2_menu_cfg *c) = s2room_menu;

/* the original's arcade pages (s2arcade.h) from a station, or, with no room, instead of it: 1 if a race follows, 0 if ESC came back */
static int arcade(struct s2_menu_cfg *c)
{
    const int chain = s2arcade_run(c);
    s2menu_store(c);
    return chain != S2ARC_BACK;
}

int s2menu_run(struct s2_menu_cfg *c)
{
    static int started;
    if (!started) {                                         /* the first start with a new save: TEAM SELECTION and ENTER YOUR NAME, as the original's every start */
        started = 1;
        if (!s2save_get(S2S_PROFILE) && s2_page_team(c) == S2P3D_DONE) s2menu_store(c);
    }
    for (;;) {                                              /* the options room is the menu: it ends with a choice, ESC or a failure */
        switch (s2menu_room(c)) {
        case S2ROOM_ARCADE:
            if (arcade(c)) return S2M_RACE;
            break;
        case S2ROOM_CHAMP:
            s2menu_store(c);
            return S2M_CHAMP;
        case S2ROOM_ATTACK:
            s2menu_store(c);
            return S2M_ATTACK;
        case S2ROOM_SETTINGS:                               /* a page of GAME SETTINGS (s2pages.h): the choices go in, and come back as it left them */
            s2menu_store(c);
            s2_page_settings();
            s2menu_load(c);
            break;
        case S2ROOM_MULTI:
            s2_page_multi();
            break;
        case S2ROOM_BACK:                                   /* ESC: TEAM SELECTION (the name after it), then the room again, also after YES to leaving the program (the core is left from the Pocket's menu) */
            s2menu_store(c);
            s2_page_team(c);
            s2menu_load(c);
            break;
        default:                                            /* the room could not be shown: the arcade pages instead */
            dlog_str("menu: the options room could not be shown, the arcade pages instead\n");
            if (arcade(c)) return S2M_RACE;
            break;
        }
    }
}
