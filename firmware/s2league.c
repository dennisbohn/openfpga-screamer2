/*
 * The championship's rules (s2league.h)
 *
 * Read from STARTL.EXE (tools/dis.sh -f; flat addresses; the numbers are checked against the tables in the file):
 *   0x1130b0  the leagues, seven numbers each: 4 1 0 1 2 1 3 | 5 2 0 2 1 1 2 | 6 3 0 3 0 1 1 | 6 0 1 0 1 0 1: races (0x1130b0 + 0x1c l), tracks opened
 *             (+4), fifth car (+8), next league (+0xc), the computers' level (+0x10, copied to the race record 0x130d4c at 0x343cf), and two
 *             numbers of the weather's draw (not used here)
 *   0x113180  the points 10 6 3 0, a row of four for every number of cars of one team (all rows alike)
 *   0x113224  the tracks in the menu's order: 0 2 3 4 5 1 6 (the race number 0x128a1c indexes it: 0x34260)
 *   0x339e7   the race's set-up: the entries (0x130c58, 0x2c bytes each): team (player's team + i) mod 4 and car (the player's car + i) mod 4 for
 *             i = 1 .. 3; in league 4 (0x128a10 = 3) the others' car is 4, the fifth (0x33b69 .. 0x33b94); case 0 (0x34260): the track, the level,
 *             4 cars, mode 2
 *   0x4337d   LEAGUE SELECTION: sets the races done (0x128a1c) and the points (0x128a74 + 0xc k) to 0 (0x434ae .. 0x434db)
 *   0x43ade   LEAGUE REPORT: the table of places from the cars' places (the player's car, if it has not finished, put last and the car that was last
 *             in his place, 0x43b05 .. 0x43c58), the points by place added (0x43d44 .. 0x43e23), the standings sorted by 0x3592e (a bubble sort that does
 *             not swap equal keys), and after the last race (0x441a5 ..): when the player's index is first, the fifth car (0x128aa0 + 0x188 team),
 *             0x128a18 = max(0x128a18, next) and 0x128a14 = max(0x128a14, tracks), or, for league 4 (tracks 0), 0x128a14 = 4 once all four teams'
 *             fifth cars are won
 */
#include "s2league.h"
#include "s2save.h"
#include <string.h>

const struct s2l_def s2l_def[S2L_LEAGUES] = {
    { 4, 1, 0, 1, 2 },
    { 5, 2, 0, 2, 1 },
    { 6, 3, 0, 3, 0 },
    { 6, 0, 1, 0, 1 },
};

const uint8_t s2l_points[4] = { 10, 6, 3, 0 };

int s2l_track(int race)
{
    return s2save_track_at(race < 0 ? 0 : race >= S2S_TRACKS ? S2S_TRACKS - 1 : race);
}

void s2l_grid(const struct s2_league *L, struct s2_pick grid[S2L_CARS])
{
    const int team0 = L->team - 1, model0 = L->car - 1;
    grid[0] = (struct s2_pick){ (uint8_t)L->team, (uint8_t)L->car };
    for (int k = 1; k < S2L_CARS; k++)
        grid[k] = (struct s2_pick){ (uint8_t)((team0 + k) % 4 + 1), (uint8_t)(L->league == S2L_LEAGUES - 1 ? 5 : (model0 + k) % 4 + 1) };
}

void s2l_begin(struct s2_league *L, int league, int team, int car)
{
    memset(L, 0, sizeof *L);
    L->league = league < 0 ? 0 : league >= S2L_LEAGUES ? S2L_LEAGUES - 1 : league;
    L->team = team < 1 ? 1 : team > 4 ? 4 : team;
    L->car = car < 1 ? 1 : car > 5 ? 5 : car;
    s2l_grid(L, L->pick);
}

void s2l_places(const int place[S2L_CARS], int player_done, struct s2l_result *r)
{
    int taken[S2L_CARS] = { 0 }, at[S2L_CARS];
    for (int p = 0; p < S2L_CARS; p++) at[p] = -1;
    for (int i = 0; i < S2L_CARS; i++) {                    /* the cars with a place of their own */
        int p = place[i] - 1;
        if (p >= 0 && p < S2L_CARS && at[p] < 0) {
            at[p] = i;
            taken[i] = 1;
        }
    }
    for (int i = 0, p = 0; i < S2L_CARS; i++) {             /* the others, in the order of their indices, into the places left */
        if (taken[i]) continue;
        while (at[p] >= 0) p++;
        at[p] = i;
    }
    if (!player_done) {                                     /* 0x43b12: last, the car that was last in his place */
        int k = 0;
        while (at[k] != 0) k++;
        if (k != S2L_CARS - 1) {
            at[k] = at[S2L_CARS - 1];
            at[S2L_CARS - 1] = 0;
        }
    }
    for (int p = 0; p < S2L_CARS; p++) r->car_at[p] = (uint8_t)at[p];
}

void s2l_add_points(struct s2_league *L, const struct s2l_result *r)
{
    for (int p = 0; p < S2L_CARS; p++) L->points[r->car_at[p] & 3] += s2l_points[p];
    L->races_done++;
}

void s2l_standings(const struct s2_league *L, int order[S2L_CARS])
{
    for (int k = 0; k < S2L_CARS; k++) order[k] = k;
    for (int a = 0; a < S2L_CARS; a++)                      /* 0x3592e: a bubble sort that swaps a neighbour only if it has fewer points */
        for (int b = 0; b < S2L_CARS - a - 1; b++)
            if (L->points[order[b]] < L->points[order[b + 1]]) {
                int t = order[b];
                order[b] = order[b + 1];
                order[b + 1] = t;
            }
}

int s2l_over(const struct s2_league *L)
{
    return L->races_done >= s2l_def[L->league].races;
}

int s2l_won(const struct s2_league *L)
{
    int order[S2L_CARS];
    s2l_standings(L, order);
    return s2l_over(L) && order[0] == 0;
}

void s2l_unlock_values(int league, int team, int *league_open, int *tracks_open, uint8_t car5[4])
{
    const struct s2l_def *d = &s2l_def[league];
    if (d->fifth) car5[team] = 1;
    if (d->next && d->next > *league_open) *league_open = d->next;
    if (d->tracks) {
        if (d->tracks > *tracks_open) *tracks_open = d->tracks;
    } else if (car5[0] && car5[1] && car5[2] && car5[3]) {
        *tracks_open = 4;
    }
}

void s2l_unlock(const struct s2_league *L)
{
    int league_open = s2save_get(S2S_LEAGUE_OPEN), tracks_open = s2save_get(S2S_TRACKS_OPEN);
    uint8_t car5[4];
    const int shown = s2save_unlock_all;                    /* (what the save holds, not what the test build shows open) */
    s2save_unlock_all = 0;
    for (int t = 0; t < 4; t++) car5[t] = (uint8_t)s2save_car5_open(t + 1);
    s2save_unlock_all = shown;
    s2l_unlock_values(L->league, L->team - 1, &league_open, &tracks_open, car5);
    s2save_set(S2S_LEAGUE_OPEN, league_open);
    s2save_set(S2S_TRACKS_OPEN, tracks_open);
    for (int t = 0; t < 4; t++) s2save_set_car5(t + 1, car5[t]);
}
