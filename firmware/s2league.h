/*
 * The championship's rules (docs/screamer2/menus.md, row 6): four leagues of 4, 5, 6 and 6 races on the tracks in the menu's order, the
 * computers' level per league, the three other teams as opponents, 10 / 6 / 3 / 0 points by place, the league won by being first on points
 * after its last race, and what winning opens. Everything is read from STARTL.EXE (the tables at 0x1130b0, 0x113180, 0x113224; the code of the
 * race's set-up 0x339e7, the report 0x43ade; the addresses are in s2league.c) and nothing here touches the hardware, so it is tested on the
 * PC (tools/s2/check_league_s2.sh). The pages are s2leaguepages.h, the flow is the viewer's.
 *
 * Progress the original keeps: in memory only the league in hand (the races done and each car's points are reset every time LEAGUE
 * SELECTION is entered, 0x434b8); what a sitting leaves for the next start is which leagues and tracks are open and the teams' fifth cars won,
 * which is the save's (s2save.h: S2S_LEAGUE_OPEN, S2S_TRACKS_OPEN, s2save_car5_open). The league in hand is not saved here either.
 */
#pragma once
#include <stdint.h>
#include "s2teams.h"

#define S2L_LEAGUES 4
#define S2L_CARS    4                   /* the player and three computers */
#define S2L_MAX_RACES 6

/* One row of the original's table at 0x1130b0 (seven numbers a league: the last two are the weather's, which this core has none of) */
struct s2l_def {
    uint8_t races;                      /* 4, 5, 6, 6 */
    uint8_t tracks;                     /* the tracks winning it opens beyond the first three (the save's S2S_TRACKS_OPEN at least this); 0: see fifth */
    uint8_t fifth;                      /* winning it gives the player's team its fifth car (league 4) */
    uint8_t next;                       /* the highest league winning it opens (S2S_LEAGUE_OPEN at least this); 0: none */
    uint8_t level;                      /* the computers' level, the row of MISC.DTR's SKILLS the race takes: 2 rookie, 1 normal, 0 pro (race_level) */
};
extern const struct s2l_def s2l_def[S2L_LEAGUES];

/* The points of a place (0 = first): 10, 6, 3, 0 (the table at 0x113180 has a row for every number of cars of one team; with four teams
   it is always this one) */
extern const uint8_t s2l_points[4];

/* The track (1 .. 7, the files' numbering) of the league's race number `race` (0-based): the menu's order from the first (0x113224: 0 2 3 4 5 1 6
   in the files' numbering from 0), the same in every league */
int s2l_track(int race);

/* The league in hand */
struct s2_league {
    int league;                         /* 0 .. 3 */
    int races_done;                     /* 0 .. s2l_def[league].races */
    int team, car;                      /* the player's: team 1 .. 4, car 1 .. 5 */
    int points[S2L_CARS];               /* by car index: 0 is the player's, 1 .. 3 the computers' */
    struct s2_pick pick[S2L_CARS];      /* each car's team and car (the race's grid) */
};

/* LEAGUE SELECTION starts a league: no races done, no points (0x434b8), the grid made (below) */
void s2l_begin(struct s2_league *L, int league, int team, int car);

/* The grid of a league race (0x339e7, the lines before the kind of game is looked at): the player's team and car in slot 0, the others the next
   teams round (team + k) and the next cars round ((car + k) mod 4, so the fifth car of the player counts as the first), and in league 4 every
   computer drives its team's fifth car. The same for every race of the league. */
void s2l_grid(const struct s2_league *L, struct s2_pick grid[S2L_CARS]);

/* A race's result: the car index at each place, first place first */
struct s2l_result {
    uint8_t car_at[S2L_CARS];
};

/* The result from what the race knows: place[i] is car i's place (1 .. 4) when the race is left, player_done whether the player's car has done
   its laps. As 0x43ade makes the table: a player who has not finished is put last and the car that was last takes his place (the others stay
   where they are). A place that is out of range or taken twice (not in a race: places are always one each) is filled by the cars in the order of their index. */
void s2l_places(const int place[S2L_CARS], int player_done, struct s2l_result *r);

/* The points of a race to the cars by their places (10 / 6 / 3 / 0), one more race done */
void s2l_add_points(struct s2_league *L, const struct s2l_result *r);

/* The standings: order[k] is the car index at rank k. By points, the most first; equal points keep the order of the car indices (the original's
   bubble sort, 0x3592e, does not swap equals), so on a tie for the lead the player, car 0, is ahead. */
void s2l_standings(const struct s2_league *L, int order[S2L_CARS]);

/* The league has had all its races; won: and the player is first in the standings */
int s2l_over(const struct s2_league *L);
int s2l_won(const struct s2_league *L);

/* What winning opens (0x44194 .. 0x442c1): the team's fifth car when the league gives it, the next league, a track more (league 4: all four
   teams' fifth cars open the seventh track). The save is the one of s2save.h. Called once, when the report shows the win. */
void s2l_unlock(const struct s2_league *L);

/* The same on plain values, for the test: league_open 0 .. 3, tracks_open 0 .. 4, car5[team 0 .. 3] */
void s2l_unlock_values(int league, int team, int *league_open, int *tracks_open, uint8_t car5[4]);
