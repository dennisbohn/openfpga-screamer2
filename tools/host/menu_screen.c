/*
 * The menu (firmware/s2menu.c) on the PC: the same code that runs on the Pocket, with a data slot read that serves the
 * real SCREAMER2.zip (its LOCAL.DAT gives the names), tests of the menu's choices (names, grid, kept choices; its run on the options room is in
 * ui_screen.c and room_screen.c). The SDRAM and the registers are mapped at their addresses as in
 * tools/host/files_screen.c. Exit code 0 if every test passed.
 *
 * Usage: menu_screen <SCREAMER2.zip> <out prefix> [<reference dir of tools/ui_reference.py> [<deflated ZIP>]]
 *        with the reference dir it also runs the 2D toolkit's tests (ui_screen.c)
 */
#define _GNU_SOURCE
#include "../s2menu.h"
#include "../s2save.h"
#include "../dlog.h"
#include "../hw.h"
#include "../slot.h"
#include "music_hw.h"
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

/* (slot.c is linked under other names for its slot_start / slot_busy / slot_result, which the music's ring uses against the model of
   music_hw.c: the music is real here, tools/menu_screens.sh) */
uint32_t slot_size(uint32_t id)
{
    if (slot_hook) slot_hook(0);
    return id == 3 ? zsize : id == 4 ? zsize2 : mhw_slot_size(id);
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

#define CPU_PER_BYTE    67                      /* cycles of the CPU's work for each byte a read brings (inflating, unpacking, building): the Pocket's logs have the room
                                                   1,487 ms for 0.85 MB read, a track 2.1 s of reading and 2.2 s of building for 2.4 MB: about 0.9 us a byte */

int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len)
{
    if (id == 21) {                                 /* the music's cue sheet */
        if (slot_hook) slot_hook(len);
        mhw_serve_cue((void *)(uintptr_t)addr);
        return 0;
    }
    if (id == 3 || id == 4) mhw_zip_read(len == SLOT_REST ? zsize - off : len, CPU_PER_BYTE);
    void *to = addr == OUTSIDE ? outside : (void *)(uintptr_t)addr;
    FILE *f = id == 4 ? zf2 : zf;
    uint32_t size = id == 4 ? zsize2 : zsize;
    if ((id != 3 && id != 4) || (off & 3) || off >= size || (len != SLOT_REST && ((len & 3) || off + len > size))) return 2;
    uint32_t n = len == SLOT_REST ? size - off : len;
    memset(to, 0, (n + 3) & ~3u);
    return fseek(f, off, SEEK_SET) || fread(to, 1, n, f) != n ? 2 : 0;
}

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

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
    CHECK(found == 3, "texts read from LOCAL.DAT: %d of 3", found);
    static const char *const tracks[S2M_TRACKS] = { "ENGLAND", "COLUMBIA", "EGYPT", "CALIFORNIA", "FINLAND", "SWITZERLAND", "THE TUNNEL" };
    for (int k = 0; k < S2M_TRACKS; k++) CHECK(!strcmp(s2menu_track_name(k + 1), tracks[k]), "track %d is %s", k + 1, s2menu_track_name(k + 1));
    CHECK(!strcmp(s2menu_team_name(1), "CONDOR") && !strcmp(s2menu_team_name(4), "ZEUS"), "teams");
    CHECK(!strcmp(s2menu_skill_name(0), "ROOKIE") && !strcmp(s2menu_skill_name(2), "PRO"), "levels");

    /* the first start: the defaults */
    struct s2_menu_cfg c;
    s2save_reset();
    s2menu_load(&c);
    CHECK(c.track == 1 && c.team == 1 && c.car == 1 && s2_menu_laps[c.laps] == 3 && c.skill == 1 && !c.manual, "defaults");

    /* the grid: the player's pick in slot 0, no team twice; the others are the original's (0x33b0e .. 0x33bc9: the loop `for k = 1 .. 3: entry k = (entry 0 + k) idiv 4's remainder` on the team
       and on the car's number from 0, a car 4 being the bonus car), here written once more from that reading with the C remainder; the old rule (the other teams in order, each with the car of
       its own number) is the negative control: it is the same for the default choice and not for the others */
    int old_differs = 0;
    for (int team = 1; team <= S2M_TEAMS; team++)
        for (int car = 1; car <= S2M_CARS; car++) {
            struct s2_menu_cfg g = { .track = 1, .team = team, .car = car, .laps = 1, .skill = 1 };
            struct s2_pick grid[4], old[4];
            int seen = 0, t0 = team - 1, m0 = car - 1, k2 = 1;
            s2menu_grid(&g, grid);
            old[0] = (struct s2_pick){ (uint8_t)team, (uint8_t)car };
            for (int t = 1; t <= S2M_TEAMS && k2 < 4; t++)
                if (t != team) old[k2++] = (struct s2_pick){ (uint8_t)t, (uint8_t)t };
            CHECK(grid[0].team == team && grid[0].car == car, "grid slot 0 for %d/%d", team, car);
            for (int k = 0; k < 4; k++) {
                CHECK(grid[k].team >= 1 && grid[k].team <= 4 && grid[k].car >= 1 && grid[k].car <= 5, "grid slot %d out of range", k);
                seen |= 1 << grid[k].team;
                if (k) CHECK(grid[k].team == (t0 + k) % 4 + 1 && grid[k].car == (m0 + k) % 4 + 1, "grid slot %d for %d/%d is %d/%d", k, team, car, grid[k].team, grid[k].car);
                old_differs += k && (old[k].team != (t0 + k) % 4 + 1 || old[k].car != (m0 + k) % 4 + 1);
            }
            CHECK(seen == 0x1E, "grid for %d/%d lacks a team", team, car);
            CHECK(!(team == 3 && car == 2) || (grid[1].team == 4 && grid[1].car == 3 && grid[2].team == 1 && grid[2].car == 4 && grid[3].team == 2 && grid[3].car == 1), "WASP's second car: the computers drive 4/3, 1/4, 2/1");
        }
    CHECK(old_differs > 0 && old_differs < 60, "negative control: the old rule differs from the original's for %d of the 60 computers' cars (the default choice is the same in both)", old_differs);
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
    struct s2_menu_cfg w = { .track = 6, .team = 3, .car = 5, .laps = 3, .skill = 2, .manual = 1 }, rd = { 0 }, dflt;      /* (the set-up list's choice is not kept in the save: s2menu_load() leaves it as it is) */
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
