/*
 * The start menu (firmware/s2menu.c) on the PC: the same code that runs on the Pocket, with a data slot read that serves the
 * real SCREAMER2.zip (its LOCAL.DAT gives the names), tests of the menu's logic (keys, grid, kept choices) and pictures of the
 * list in several states as raw RGB frames for tools/rgb2png.py. The SDRAM and the registers are mapped at their addresses as in
 * tools/host/files_screen.c. Exit code 0 if every test passed.
 *
 * Usage: menu_screen <SCREAMER2.zip> <out prefix> [<reference dir of tools/ui_reference.py> [<deflated ZIP>]]
 *        writes <out prefix>_<state>.rgb; with the reference dir it also runs the 2D toolkit's tests (ui_screen.c)
 */
#define _GNU_SOURCE
#include "../s2menu.h"
#include "../s2save.h"
#include "../dlog.h"
#include "../hw.h"
#include "../slot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SCRATCH     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000))

static FILE *zf, *zf2;                  /* slot 3: the ZIP; slot 4: a deflated one for the toolkit's test */
static uint32_t zsize, zsize2;
static struct zip zip, zip2;
static int failures;

int ui_tests(struct zip *z, struct zip *deflated, const char *ref, const char *prefix);

uint32_t slot_size(uint32_t id)
{
    return id == 3 ? zsize : id == 4 ? zsize2 : 0;
}

/* (s2save.c links in for the choices; s2save_load() is not called here: tools/s2/check_save_s2.sh tests it) */
void slot_set_size(uint32_t id, uint32_t size)
{
    (void)id;
    (void)size;
}

/* zip.c's buffers (not in the SDRAM here): see files_screen.c */
#define OUTSIDE 0xE0000000u
static uint8_t *outside;

uint32_t zip_addr(const void *p)
{
    if ((uintptr_t)p >= SDRAM_BASE && (uintptr_t)p < SDRAM_BASE + SDRAM_SIZE) return (uint32_t)(uintptr_t)p;
    outside = (uint8_t *)p;
    return OUTSIDE;
}

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    void *to = addr == OUTSIDE ? outside : (void *)(uintptr_t)addr;
    FILE *f = id == 4 ? zf2 : zf;
    uint32_t size = id == 4 ? zsize2 : zsize;
    if ((id != 3 && id != 4) || (off & 3) || off >= size || (len != SLOT_REST && ((len & 3) || off + len > size))) return 2;
    uint32_t n = len == SLOT_REST ? size - off : len;
    memset(to, 0, (n + 3) & ~3u);
    return fseek(f, off, SEEK_SET) || fread(to, 1, n, f) != n ? 2 : 0;
}

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void shot(const char *prefix, const char *name, const struct s2_menu_cfg *c, int cursor)
{
    char path[256];
    snprintf(path, sizeof path, "%s_%s.rgb", prefix, name);
    s2menu_draw(FB(0), c, cursor);
    FILE *o = fopen(path, "wb");
    const volatile uint8_t *fb = FB(0);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t col = VID_PAL[fb[i]];
        uint8_t rgb[3] = { (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
}

static int press(struct s2_menu_cfg *c, int *cursor, uint32_t keys)
{
    return s2menu_key(c, cursor, keys);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <SCREAMER2.zip> <out prefix> [<reference dir> [<deflated ZIP>]]\n", argv[0]); return 2; }
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE)
        || mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED
        || mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    if (!(zf = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
    fseek(zf, 0, SEEK_END);
    zsize = (uint32_t)ftell(zf);
    dlog_init();
    int r = zip_open(&zip, 3, SCRATCH, SCRATCH_MAX);
    CHECK(r == ZIP_OK, "zip_open %d", r);
    int found = s2menu_init(&zip);
    CHECK(found == 8, "texts read from LOCAL.DAT: %d of 8", found);
    static const char *const tracks[S2M_TRACKS] = { "ENGLAND", "COLUMBIA", "EGYPT", "CALIFORNIA", "FINLAND", "SWITZERLAND", "THE TUNNEL" };
    for (int k = 0; k < S2M_TRACKS; k++) CHECK(!strcmp(s2menu_track_name(k + 1), tracks[k]), "track %d is %s", k + 1, s2menu_track_name(k + 1));
    CHECK(!strcmp(s2menu_team_name(1), "CONDOR") && !strcmp(s2menu_team_name(4), "ZEUS"), "teams");
    CHECK(!strcmp(s2menu_skill_name(0), "ROOKIE") && !strcmp(s2menu_skill_name(2), "PRO"), "levels");

    /* the first start: the defaults, the cursor on ARCADE */
    struct s2_menu_cfg c;
    int cursor = S2M_ROW_ARCADE;
    s2save_reset();
    s2menu_load(&c);
    CHECK(c.track == 1 && c.team == 1 && c.car == 1 && s2_menu_laps[c.laps] == 3 && c.skill == 1 && !c.manual, "defaults");
    shot(argv[2], "first", &c, cursor);
    CHECK(press(&c, &cursor, KEY_B) == S2M_ARCADE && press(&c, &cursor, KEY_START) == S2M_ARCADE && press(&c, &cursor, KEY_A) == S2M_ARCADE,
          "B, Start and A on ARCADE go on to the pages");
    CHECK(press(&c, &cursor, KEY_SELECT) == S2M_NONE && press(&c, &cursor, 0) == S2M_NONE, "Select and no key do nothing");

    /* the player's wish: track 4 in two presses of the menu from START RACE (R is three tracks on, A starts) */
    cursor = S2M_ROW_RACE;
    CHECK(press(&c, &cursor, KEY_R) == S2M_NONE && c.track == 4, "R: track %d", c.track);
    CHECK(press(&c, &cursor, KEY_A) == S2M_RACE, "A starts the race");
    shot(argv[2], "track4", &c, cursor);

    /* every track is at most two moves of d-pad left/right (one step, on START RACE) and L / R (three) away from every other one */
    for (int from = 1; from <= S2M_TRACKS; from++)
        for (int to = 1; to <= S2M_TRACKS; to++) {
            int best = 99;
            static const uint32_t moves[4] = { KEY_LEFT, KEY_RIGHT, KEY_L, KEY_R };
            for (int a = -1; a < 4; a++)
                for (int b = -1; b < 4; b++) {
                    struct s2_menu_cfg t = { .track = from, .team = 1, .car = 1, .laps = 1, .skill = 1 };
                    int cur = S2M_ROW_RACE, n = 0;
                    if (a >= 0) { press(&t, &cur, moves[a]); n++; }
                    if (b >= 0) { press(&t, &cur, moves[b]); n++; }
                    if (t.track == to && n < best) best = n;
                }
            CHECK(best <= 2, "track %d to %d needs %d moves", from, to, best);
        }

    /* the rows and their values */
    cursor = S2M_ROW_ARCADE;
    c = (struct s2_menu_cfg){ .track = 1, .team = 1, .car = 1, .laps = 1, .skill = 1, .manual = 0 };
    press(&c, &cursor, KEY_LEFT);
    CHECK(c.track == 1 && c.laps == 1 && c.skill == 1 && c.team == 1, "left on ARCADE changes nothing");
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_CHAMP, "down: the CHAMPIONSHIP row");
    press(&c, &cursor, KEY_LEFT);
    press(&c, &cursor, KEY_RIGHT);
    CHECK(c.track == 1 && c.laps == 1 && c.skill == 1, "left / right on CHAMPIONSHIP change nothing");
    shot(argv[2], "champrow", &c, cursor);
    CHECK(press(&c, &cursor, KEY_B) == S2M_CHAMP && press(&c, &cursor, KEY_START) == S2M_CHAMP && press(&c, &cursor, KEY_A) == S2M_CHAMP,
          "B, Start and A on CHAMPIONSHIP go to the league pages");
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_LAPS, "down: laps row");
    press(&c, &cursor, KEY_RIGHT);
    CHECK(s2_menu_laps[c.laps] == 5, "laps after one right: %d", s2_menu_laps[c.laps]);
    press(&c, &cursor, KEY_LEFT);
    press(&c, &cursor, KEY_LEFT);
    press(&c, &cursor, KEY_LEFT);
    CHECK(s2_menu_laps[c.laps] == 10, "laps wrap from 1 to 10 (%d)", s2_menu_laps[c.laps]);
    shot(argv[2], "laps", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    press(&c, &cursor, KEY_RIGHT);
    CHECK(cursor == S2M_ROW_SKILL && c.skill == 2, "skill %d", c.skill);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_RACE, "start race row");
    press(&c, &cursor, KEY_RIGHT);
    CHECK(c.track == 2, "right on START RACE: track %d", c.track);
    press(&c, &cursor, KEY_LEFT);
    press(&c, &cursor, KEY_LEFT);
    CHECK(c.track == 7, "track wraps from 1 to 7 (%d)", c.track);
    shot(argv[2], "startrow", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_WATCH && press(&c, &cursor, KEY_START) == S2M_WATCH, "watch row");
    shot(argv[2], "watchrow", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_SETTINGS && press(&c, &cursor, KEY_A) == S2M_SETTINGS, "GAME SETTINGS row");
    shot(argv[2], "settingsrow", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_SCORES && press(&c, &cursor, KEY_START) == S2M_SCORES, "HI SCORES row");
    shot(argv[2], "scoresrow", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_TEAM && press(&c, &cursor, KEY_B) == S2M_TEAM && press(&c, &cursor, KEY_A) == S2M_TEAM, "TEAM SELECTION row");
    shot(argv[2], "teamrow", &c, cursor);
    press(&c, &cursor, KEY_DOWN);
    CHECK(cursor == S2M_ROW_ARCADE, "the cursor wraps");
    CHECK(press(&c, &cursor, KEY_X) == S2M_WATCH, "X watches");
    press(&c, &cursor, KEY_UP);
    CHECK(cursor == S2M_ROW_TEAM, "up from ARCADE wraps to the last row");
    cursor = S2M_ROW_SKILL;
    CHECK(press(&c, &cursor, KEY_START) == S2M_RACE, "Start on a setting starts the race");

    /* the grid: the player's pick in slot 0, the three other teams in the others, no team twice; the default is race.c's */
    for (int team = 1; team <= S2M_TEAMS; team++)
        for (int car = 1; car <= S2M_CARS; car++) {
            struct s2_menu_cfg g = { .track = 1, .team = team, .car = car, .laps = 1, .skill = 1 };
            struct s2_pick grid[4];
            int seen = 0;
            s2menu_grid(&g, grid);
            CHECK(grid[0].team == team && grid[0].car == car, "grid slot 0 for %d/%d", team, car);
            for (int k = 0; k < 4; k++) {
                CHECK(grid[k].team >= 1 && grid[k].team <= 4 && grid[k].car >= 1 && grid[k].car <= 5, "grid slot %d out of range", k);
                seen |= 1 << grid[k].team;
            }
            CHECK(seen == 0x1E, "grid for %d/%d lacks a team", team, car);
        }
    {
        struct s2_menu_cfg d;
        struct s2_pick grid[4];
        s2menu_defaults(&d);
        s2menu_grid(&d, grid);
        for (int k = 0; k < 4; k++) CHECK(grid[k].team == k + 1 && grid[k].car == k + 1, "default grid slot %d", k);
    }

    /* kept: a round trip through the save (s2save.h; its own test is tools/s2/check_save_s2.sh), the ranges are the menu's */
    CHECK(s2save_count(S2S_TRACK) == S2M_TRACKS && s2save_count(S2S_TEAM) == S2M_TEAMS && s2save_count(S2S_CAR) == S2M_CARS &&
          s2save_count(S2S_LAPS) == S2M_LAPS && s2save_count(S2S_SKILL) == S2M_SKILLS && s2save_count(S2S_TRANS) == 2, "the save's ranges are the menu's");
    struct s2_menu_cfg w = { .track = 6, .team = 3, .car = 5, .laps = 3, .skill = 2, .manual = 1 }, rd, dflt;
    s2save_reset();
    s2menu_store(&w);
    s2menu_load(&rd);
    CHECK(!memcmp(&w, &rd, sizeof w), "round trip");
    s2save_reset();
    s2menu_load(&rd);
    s2menu_defaults(&dflt);
    CHECK(!memcmp(&dflt, &rd, sizeof rd), "no choices kept: the defaults");
    s2menu_store(&(struct s2_menu_cfg){ .track = 99, .team = 77, .car = -4, .laps = 12, .skill = -1, .manual = 5 });
    s2menu_load(&rd);
    CHECK(rd.track == 7 && rd.car == 1 && rd.team == 4 && rd.laps == 3 && rd.skill == 0 && rd.manual == 1, "out of range values are clamped");

    printf("%s", (const char *)(uintptr_t)DLOG_BASE + DLOG_HEAD);
    printf(failures ? "menu: %d tests FAILED\n" : "menu: all tests passed\n", failures);
    if (argc > 3) {                                                 /* the 2D toolkit: pictures, text, loading, pause, runner, sounds */
        struct zip *def = 0;
        if (argc > 4 && (zf2 = fopen(argv[4], "rb"))) {
            fseek(zf2, 0, SEEK_END);
            zsize2 = (uint32_t)ftell(zf2);
            CHECK(zip_open(&zip2, 4, SCRATCH, SCRATCH_MAX) == ZIP_OK, "zip_open of the deflated ZIP");
            def = &zip2;
        }
        failures += ui_tests(&zip, def, argv[3], argv[2]);
    }
    return failures != 0;
}
