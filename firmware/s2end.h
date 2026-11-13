/*
 * The end of the race (STARTL.EXE 0x4720c / 0x47636, docs/screamer2/menus.md section 6): over the race that goes on behind it, for four
 * seconds CONGRATULATIONS ! (first place only), "1ST POSITION" and the total time, then the track's banner, a darkened band with RACE AGAIN
 * and EXIT (the replay's REPLAY comes with the replay) and the track's best times for the length of the race, in a darkened box, five rows
 * in view: the player's time has been put into the lists at that moment (s2save.h, as 0x353cb does) and its row, lit, is brought
 * into view. Left / right choose, B or Start take (the original's ENTER).
 *
 * Time attack's end (0x48b4c, `attack` in s2_end_in): the arcade's, but the first four seconds show TOTAL, the total time, LAP TIME and the best lap (font 0 at y 60,
 * 80, 100, 120) in place of CONGRATULATIONS / place / time, and the player's times go into time attack's lists (S2S_KIND_ATTACK: only the three-lap totals and the laps).
 *
 * The championship's end (0x48041, `league` in s2_end_in): the same four seconds, then the banner, the darkened band with CONTINUE alone in the
 * middle (the original's REPLAY comes with the replay) and, in a darkened box, a list of the cars that have done their laps: place, name (the
 * player's, CPU for the computers), the car's side view, its time and the points of its place (10 / 6 / 3 / 0), the rows by place; B or Start end
 * it. The player's time and best lap go into the championship's lists (S2S_KIND_LEAGUE) instead of the arcade's.
 *
 * REPLAY (STARTL.EXE 0x47636, 0x48041: the choice is there when the race was recorded whole, 0x1330b4 / 0x1330d0, or is itself a replay, mode bit 0x100): the arcade's
 * choices are REPLAY, RACE AGAIN and EXIT in thirds of the band (boxes of 106 from x 0, 106, 212), REPLAY first and lit; the championship's REPLAY and CONTINUE in
 * halves. The end screen of the replay is the same screen: the times are not put into the lists again (0x353cb is not called in a replay) and the list is
 * where the race's own screen left it (its row lit, scrolled), the choice starts on REPLAY again.
 *
 * It draws into the back buffer of the race after the rasterizer is done (darkening what the race drew, then the blitter's text and sprites
 * of the toolkit, s2gfx.h) and costs the race frame nothing until the player has finished.
 */
#pragma once
#include <stdint.h>

struct s2_end_car {
    char name[4];                       /* the player's name or "CPU", a string (no padding spaces) */
    uint8_t team, car;                  /* 1 .. 4 and 1 .. 5 as the files number them */
    uint8_t place;                      /* 1 .. 4 */
    uint8_t done;                       /* the car has done all its laps: it is listed */
    float time;                         /* its time */
};

struct s2_end_in {
    int track;                          /* 1 .. 7 */
    int laps;                           /* the race's laps: 1, 3, 5, 10 */
    int place;                          /* the player's place, 1 .. */
    float time;                         /* his total time in seconds */
    float best_lap;                     /* his best lap, 0 or less if none */
    int team, car;                      /* his team 1 .. 4 and car 1 .. 5 (the files' numbering) */
    int league;                         /* 1: the championship's end: the four cars below are shown, CONTINUE is the only choice */
    int attack;                         /* 1: time attack's end (0x48b4c): TOTAL, the time, LAP TIME and the best lap for the first four seconds, the lists are time attack's */
    int replay;                         /* 1: the race can be run again: REPLAY is the first choice (never in time attack) */
    int replaying;                      /* 1: this race is a replay: nothing goes into the lists, the list keeps its row and its scroll */
    struct s2_end_car cars[4];
};

enum { S2END_NONE, S2END_AGAIN, S2END_EXIT, S2END_CONTINUE, S2END_REPLAY };

/* Combat mode's end (two players on the screen's halves; measured on the running original, tools/s2/dosbox_arcade.py --combat): the half of a player who has done his laps shows
   his place and time ("1ST POSITION", the time: the end screen's letters, centred at the half's rows 43 and 60) while the other still races; once both have and `offer` is set
   RACE AGAIN and EXIT in halves at y 184, as the arcade's end has them: LEFT / RIGHT choose, ENTER takes it. One call a frame, after the rasterizer is done, into the
   race's buffer; vf and press as s2end_frame()'s. No list of times: the original keeps none of these races. Returns S2END_AGAIN, S2END_EXIT or S2END_NONE. */
struct s2_end_split {
    int done[2], place[2];              /* each player: he has done his laps; his place, 1 or 2 */
    float time[2];                      /* his total time */
    int offer;                          /* the choices are up */
};
int s2end_split(int buffer, const struct s2_end_split *in, uint32_t vf, uint32_t press);

/* One frame of the end screen, drawn into framebuffer `buffer` (fb) of the race with its palette pal (256 x 0x00RRGGBB). fin_frames: the
   frames the player has been finished so far (0 on the first: the screen starts over, the table for darkening is made); vf: the video frame
   counter (the 16 bits of VID_FRAME, 50 a second: the clocks run by it); press: the buttons that went down in this frame. Returns
   S2END_AGAIN or S2END_EXIT once the player has chosen (the race again from the grid, the start menu), S2END_CONTINUE in the championship (the race
   is over, on to the report), S2END_REPLAY (the race run again from its recording), else S2END_NONE. */
int s2end_frame(volatile uint8_t *fb, int buffer, const uint32_t *pal, const struct s2_end_in *in, uint32_t fin_frames, uint32_t vf,
                uint32_t press);
