/*
 * Host test of the championship's rules (firmware/s2league.c), run by tools/s2/check_league_s2.sh: the firmware's own code with the save's
 * window mapped (s2save.c) and the data slot calls faked. Pure logic, no game files:
 *
 *   tables      the four leagues' numbers against the table of STARTL.EXE at 0x1130b0 (as read: races 4 5 6 6, level 2 1 0 1, the unlocks), the points
 *               10 6 3 0, the tracks of every league's races (the menu's order from the first: 1 3 4 5 6 2)
 *   grid        every team x car x league: the player in slot 0, the other three teams once each, the cars (car + k) mod 4, league 4 all on the fifth
 *   places      all 24 orders of the four cars x the player finished or not: always a permutation, the player last when he has not finished and the
 *               car that was last in his place, the others untouched; places taken twice or out of range filled by index
 *   points      every race gives 19 points, the totals add up over a whole league, races done counts, over / won at the right time
 *   standings   against a plain stable insertion sort of the points, 20,000 random sets, and the ties: all equal keeps the order, the player wins a tie
 *   unlocks     winning each league in turn on plain values and through the save: next league, tracks, the fifth car of the team, league 4 with one
 *               team and with all four (the seventh track), a lost league opens nothing, nothing opened is ever closed again
 *
 * Exit status 1 if a check failed.
 */
#define _GNU_SOURCE
#include "../../firmware/s2league.h"
#include "../../firmware/s2save.h"
#include "../../firmware/save.h"
#include "../../firmware/champ.h"
#include "../../firmware/slot.h"
#include "../../firmware/dlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

uint32_t slot_size(uint32_t id) { (void)id; return 0; }
int slot_read(uint32_t id, uint32_t off, uint32_t addr, uint32_t len) { (void)id; (void)off; (void)addr; (void)len; return 2; }
void slot_set_size(uint32_t id, uint32_t size) { (void)id; (void)size; }
void dlog_str(const char *s) { (void)s; }
void dlog_dec(int32_t v) { (void)v; }

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 30) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void test_tables(void)
{
    /* the table of STARTL.EXE at 0x1130b0 (7 numbers a league, the last two of the weather) and the other two tables, as dumped from the flat image */
    static const int exe[28] = { 4, 1, 0, 1, 2, 1, 3, 5, 2, 0, 2, 1, 1, 2, 6, 3, 0, 3, 0, 1, 1, 6, 0, 1, 0, 1, 0, 1 };
    for (int l = 0; l < S2L_LEAGUES; l++) {
        const struct s2l_def *d = &s2l_def[l];
        CHECK(d->races == exe[7 * l] && d->tracks == exe[7 * l + 1] && d->fifth == exe[7 * l + 2] && d->next == exe[7 * l + 3] && d->level == exe[7 * l + 4],
              "league %d: %d %d %d %d %d", l + 1, d->races, d->tracks, d->fifth, d->next, d->level);
        CHECK(d->races <= S2L_MAX_RACES, "league %d has %d races", l + 1, d->races);
    }
    static const uint8_t points[4] = { 10, 6, 3, 0 };
    CHECK(!memcmp(s2l_points, points, 4), "points");
    static const int order[7] = { 1, 3, 4, 5, 6, 2, 7 };      /* 0x113224: 0 2 3 4 5 1 6, the files' numbers from 1 */
    for (int l = 0; l < S2L_LEAGUES; l++)
        for (int r = 0; r < s2l_def[l].races; r++) CHECK(s2l_track(r) == order[r], "league %d race %d on track %d", l + 1, r + 1, s2l_track(r));
    CHECK(s2l_track(-1) == 1 && s2l_track(99) == 7, "track numbers out of range are clamped");
    /* a league never reaches the seventh track (the Tunnel), which only league 4's all-teams win opens */
    for (int l = 0; l < S2L_LEAGUES; l++) CHECK(s2l_track(s2l_def[l].races - 1) != 7, "league %d reaches the seventh track", l + 1);
}

static void test_grid(void)
{
    for (int league = 0; league < S2L_LEAGUES; league++)
        for (int team = 1; team <= 4; team++)
            for (int car = 1; car <= 5; car++) {
                struct s2_league L;
                struct s2_pick g[S2L_CARS];
                s2l_begin(&L, league, team, car);
                s2l_grid(&L, g);
                CHECK(g[0].team == team && g[0].car == car, "slot 0 of %d/%d/%d", league, team, car);
                int seen = 0;
                for (int k = 0; k < S2L_CARS; k++) {
                    seen |= 1 << g[k].team;
                    CHECK(g[k].team >= 1 && g[k].team <= 4 && g[k].car >= 1 && g[k].car <= 5, "slot %d out of range", k);
                    CHECK(!memcmp(&g[k], &L.pick[k], sizeof g[k]), "begin keeps the grid");
                    if (k) {
                        /* the original's rule written the other way round: team index (t + k) mod 4, car index (c + k) mod 4, league 4's computers on the fifth */
                        int t = ((team - 1) + k) % 4 + 1, c = league == 3 ? 5 : ((car - 1) + k) % 4 + 1;
                        CHECK(g[k].team == t && g[k].car == c, "league %d player %d/%d: slot %d is %d/%d, expected %d/%d", league + 1, team, car, k, g[k].team, g[k].car, t, c);
                    }
                }
                CHECK(seen == 0x1E, "grid of %d/%d/%d lacks a team", league, team, car);
            }
    struct s2_league L;
    s2l_begin(&L, 9, 0, 9);                                         /* out of range: clamped */
    CHECK(L.league == 3 && L.team == 1 && L.car == 5, "begin clamps");
}

static void test_places(void)
{
    int perm[24][4], n = 0;
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 4; c++)
                for (int d = 0; d < 4; d++)
                    if (a != b && a != c && a != d && b != c && b != d && c != d) {
                        perm[n][0] = a; perm[n][1] = b; perm[n][2] = c; perm[n][3] = d;
                        n++;
                    }
    CHECK(n == 24, "permutations");
    for (int i = 0; i < 24; i++)
        for (int done = 0; done < 2; done++) {
            int place[4];                                           /* place[car] = the place (1 .. 4) of the car */
            for (int p = 0; p < 4; p++) place[perm[i][p]] = p + 1;
            struct s2l_result r;
            s2l_places(place, done, &r);
            int seen = 0;
            for (int p = 0; p < 4; p++) seen |= 1 << r.car_at[p];
            CHECK(seen == 0xF, "order %d: not a permutation", i);
            if (done) {
                for (int p = 0; p < 4; p++) CHECK(r.car_at[p] == perm[i][p], "order %d: a finished player changes the order", i);
            } else {
                CHECK(r.car_at[3] == 0, "order %d: the player who left is not last", i);
                int k = 0;
                while (perm[i][k] != 0) k++;
                for (int p = 0; p < 3; p++) {
                    int want = p == k ? perm[i][3] : perm[i][p];    /* the car that was last takes his place, the others stay */
                    CHECK(r.car_at[p] == want || k == 3, "order %d: place %d is car %d, expected %d", i, p + 1, r.car_at[p], want);
                }
                if (k == 3) for (int p = 0; p < 4; p++) CHECK(r.car_at[p] == perm[i][p], "order %d: already last, nothing moves", i);
            }
        }
    /* places taken twice, out of range or missing */
    int bad[3][4] = { { 1, 1, 1, 1 }, { 0, 9, 2, -3 }, { 4, 4, 3, 2 } };
    for (int i = 0; i < 3; i++) {
        struct s2l_result r;
        s2l_places(bad[i], 1, &r);
        int seen = 0;
        for (int p = 0; p < 4; p++) seen |= 1 << r.car_at[p];
        CHECK(seen == 0xF, "bad places %d: not a permutation", i);
    }
    struct s2l_result r;
    s2l_places((int[4]){ 1, 1, 1, 1 }, 1, &r);
    CHECK(r.car_at[0] == 0 && r.car_at[1] == 1 && r.car_at[2] == 2 && r.car_at[3] == 3, "all first: by index");
}

static void test_points(void)
{
    for (int league = 0; league < S2L_LEAGUES; league++) {
        struct s2_league L;
        s2l_begin(&L, league, 2, 3);
        int total = 0;
        unsigned seed = 12345u + (unsigned)league;
        CHECK(!s2l_over(&L) && !s2l_won(&L), "a new league is not over");
        for (int race = 0; race < s2l_def[league].races; race++) {
            int place[4] = { 1, 2, 3, 4 };
            for (int i = 3; i > 0; i--) {                           /* a shuffle of the places */
                seed = seed * 1103515245u + 12345u;
                int j = (int)(seed >> 16) % (i + 1), t = place[i];
                place[i] = place[j];
                place[j] = t;
            }
            struct s2l_result r;
            s2l_places(place, 1, &r);
            s2l_add_points(&L, &r);
            total += 19;
            int sum = 0;
            for (int c = 0; c < 4; c++) sum += L.points[c];
            CHECK(sum == total && L.races_done == race + 1, "league %d after race %d: %d points, %d races done", league + 1, race + 1, sum, L.races_done);
            CHECK(s2l_over(&L) == (race + 1 == s2l_def[league].races), "over after race %d of league %d", race + 1, league + 1);
        }
        CHECK(s2l_over(&L), "league %d is over", league + 1);
    }
    /* the player first in all the races wins, last in all of them does not; a win needs the league to be over */
    for (int league = 0; league < S2L_LEAGUES; league++) {
        struct s2_league W, B;
        s2l_begin(&W, league, 1, 1);
        s2l_begin(&B, league, 1, 1);
        for (int race = 0; race < s2l_def[league].races; race++) {
            CHECK(!s2l_won(&W), "league %d: won before the last race", league + 1);
            s2l_add_points(&W, &(struct s2l_result){ { 0, 1, 2, 3 } });
            s2l_add_points(&B, &(struct s2l_result){ { 1, 2, 3, 0 } });
        }
        CHECK(s2l_won(&W) && !s2l_won(&B), "league %d: winning all races wins it (%d), losing all does not (%d)", league + 1, s2l_won(&W), s2l_won(&B));
        CHECK(W.points[0] == 10 * s2l_def[league].races && B.points[0] == 0, "league %d points %d %d", league + 1, W.points[0], B.points[0]);
    }
    /* a tie for the lead goes to the player: 2 races, first and third, then second and ... (10 + 3 = 13 against 6 + 10 + ... ) */
    struct s2_league T;
    s2l_begin(&T, 0, 1, 1);
    s2l_add_points(&T, &(struct s2l_result){ { 0, 1, 2, 3 } });     /* the player 10, car 1: 6 */
    s2l_add_points(&T, &(struct s2l_result){ { 1, 0, 2, 3 } });     /* the player 16, car 1: 16 */
    s2l_add_points(&T, &(struct s2l_result){ { 2, 3, 0, 1 } });     /* the player 19, car 1: 16, car 2: 10 */
    s2l_add_points(&T, &(struct s2l_result){ { 1, 2, 3, 0 } });     /* the player 19, car 1: 26 */
    CHECK(s2l_over(&T) && !s2l_won(&T), "car 1 leads with %d against the player's %d", T.points[1], T.points[0]);
    struct s2_league U = T;
    U.points[0] = U.points[1] = 26;
    CHECK(s2l_won(&U), "a tie for the lead is the player's");
    U.points[2] = 30;
    CHECK(!s2l_won(&U), "a car ahead of the player wins");
}

static void test_standings(void)
{
    struct s2_league L;
    memset(&L, 0, sizeof L);
    int order[4];
    s2l_standings(&L, order);
    CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3, "all equal keeps the order");
    srand(7);
    for (int n = 0; n < 20000; n++) {
        for (int c = 0; c < 4; c++) L.points[c] = rand() % 7 * 3;
        int model[4] = { 0, 1, 2, 3 };
        for (int i = 1; i < 4; i++) {                               /* a stable insertion sort, the most first */
            int t = model[i], j = i;
            while (j > 0 && L.points[model[j - 1]] < L.points[t]) {
                model[j] = model[j - 1];
                j--;
            }
            model[j] = t;
        }
        s2l_standings(&L, order);
        CHECK(!memcmp(order, model, sizeof model), "points %d %d %d %d: order %d %d %d %d, expected %d %d %d %d", L.points[0], L.points[1], L.points[2], L.points[3],
              order[0], order[1], order[2], order[3], model[0], model[1], model[2], model[3]);
        if (failures) break;
    }
}

static void test_unlocks(void)
{
    /* plain values: league n opens league n and a track more; league 4 gives the team's fifth car and, with all four teams', the seventh track */
    int lo = 0, to = 0;
    uint8_t c5[4] = { 0 };
    s2l_unlock_values(0, 1, &lo, &to, c5);
    CHECK(lo == 1 && to == 1 && !c5[0] && !c5[1] && !c5[2] && !c5[3], "league 1 won: league %d, tracks %d", lo, to);
    s2l_unlock_values(1, 1, &lo, &to, c5);
    CHECK(lo == 2 && to == 2, "league 2 won: league %d, tracks %d", lo, to);
    s2l_unlock_values(2, 1, &lo, &to, c5);
    CHECK(lo == 3 && to == 3, "league 3 won: league %d, tracks %d", lo, to);
    s2l_unlock_values(3, 1, &lo, &to, c5);
    CHECK(lo == 3 && to == 3 && c5[1] && !c5[0] && !c5[2] && !c5[3], "league 4 won with one team: the fifth car, no new track (%d %d)", lo, to);
    s2l_unlock_values(3, 0, &lo, &to, c5);
    s2l_unlock_values(3, 2, &lo, &to, c5);
    CHECK(to == 3, "league 4 won with three teams: still %d tracks", to);
    s2l_unlock_values(3, 3, &lo, &to, c5);
    CHECK(to == 4 && c5[0] && c5[1] && c5[2] && c5[3], "league 4 won with all four teams: the seventh track (%d)", to);
    /* nothing opened is closed again: league 1 won again with everything open changes nothing */
    s2l_unlock_values(0, 0, &lo, &to, c5);
    CHECK(lo == 3 && to == 4, "an earlier league won again closes nothing (%d %d)", lo, to);
    /* any order of winning the leagues, any team: the numbers never go down and end at 3 and 3 (4 with all teams) */
    for (int mask = 0; mask < 1 << 8; mask++) {
        int l = 0, t = 0;
        uint8_t f[4] = { 0 };
        for (int step = 0; step < 8; step++) {
            int league = (mask >> (2 * (step >> 1)) & 3), team = (step & 1) * 2 + (mask >> 7 & 1);
            int l0 = l, t0 = t;
            s2l_unlock_values(league, team & 3, &l, &t, f);
            CHECK(l >= l0 && t >= t0 && l <= 3 && t <= 4, "values went down or out of range");
        }
    }

    /* through the save, with the real s2save: the win writes the three things, and unlock_all (the test build) neither hides nor changes what the save holds */
    for (int unlock = 0; unlock < 2; unlock++) {
        s2save_reset();
        s2save_unlock_all = unlock;
        struct s2_league L;
        for (int league = 0; league < S2L_LEAGUES; league++) {
            s2l_begin(&L, league, 2, 1);
            s2l_unlock(&L);
            CHECK(s2save_get(S2S_LEAGUE_OPEN) == (league < 3 ? league + 1 : 3), "after league %d: S2S_LEAGUE_OPEN %d", league + 1, s2save_get(S2S_LEAGUE_OPEN));
            CHECK(s2save_get(S2S_TRACKS_OPEN) == (league < 3 ? league + 1 : 3), "after league %d: S2S_TRACKS_OPEN %d", league + 1, s2save_get(S2S_TRACKS_OPEN));
            s2save_unlock_all = 0;
            CHECK((s2save_car5_open(2) != 0) == (league == 3) && !s2save_car5_open(1) && !s2save_car5_open(3) && !s2save_car5_open(4),
                  "after league %d: the fifth cars %d %d %d %d", league + 1, s2save_car5_open(1), s2save_car5_open(2), s2save_car5_open(3), s2save_car5_open(4));
            s2save_unlock_all = unlock;
        }
        for (int team = 1; team <= 4; team++) {
            s2l_begin(&L, 3, team, 1);
            s2l_unlock(&L);
        }
        CHECK(s2save_get(S2S_TRACKS_OPEN) == 4, "all four teams won league 4: tracks %d", s2save_get(S2S_TRACKS_OPEN));
        s2save_unlock_all = 0;
        CHECK(s2save_car5_open(1) && s2save_car5_open(2) && s2save_car5_open(3) && s2save_car5_open(4), "the four fifth cars");
        CHECK(s2save_track_open(2) && s2save_track_open(7) && s2save_league_open(3), "the save opens the Columbia, the Tunnel and league 4");
        s2save_unlock_all = unlock;
    }
    /* the gating the pages use: a new save has league 1 and three tracks open (unlock_all off) */
    s2save_reset();
    s2save_unlock_all = 0;
    CHECK(s2save_league_open(0) && !s2save_league_open(1) && !s2save_league_open(3), "a new save: league 1 only");
    CHECK(s2save_track_open(1) && s2save_track_open(3) && s2save_track_open(4) && !s2save_track_open(5), "a new save: three tracks (England, Egypt, California)");
    s2save_unlock_all = S2SAVE_UNLOCK_ALL;
}

int main(void)
{
    void *m = mmap((void *)(uintptr_t)SAVE_CHOICE_ADDR, 0x20000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (m == MAP_FAILED || m != (void *)(uintptr_t)SAVE_CHOICE_ADDR) {
        perror("mmap of the CHOICE.DAT window");
        return 2;
    }
    s2save_reset();
    typedef void (*test_fn)(void);
    static const struct { const char *name; test_fn fn; } tests[] = {
        { "tables", test_tables }, { "grid", test_grid }, { "places", test_places }, { "points", test_points }, { "standings", test_standings },
        { "unlocks", test_unlocks },
    };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        int before = failures, c0 = checks;
        tests[i].fn();
        printf("%-12s %s (%d checks)\n", tests[i].name, failures == before ? "ok" : "FAILED", checks - c0);
    }
    printf(failures ? "league: %d of %d checks FAILED\n" : "league: all %d checks passed\n", failures ? failures : checks, checks);
    return failures != 0;
}
