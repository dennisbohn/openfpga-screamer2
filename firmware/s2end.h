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
    struct s2_end_car cars[4];
};

enum { S2END_NONE, S2END_AGAIN, S2END_EXIT, S2END_CONTINUE };

/* One frame of the end screen, drawn into framebuffer `buffer` (fb) of the race with its palette pal (256 x 0x00RRGGBB). fin_frames: the
   frames the player has been finished so far (0 on the first: the screen starts over, the table for darkening is made); vf: the video frame
   counter (the 16 bits of VID_FRAME, 50 a second: the clocks run by it); press: the buttons that went down in this frame. Returns
   S2END_AGAIN or S2END_EXIT once the player has chosen (the race again from the grid, the start menu), S2END_CONTINUE in the championship (the race
   is over, on to the report), else S2END_NONE. */
int s2end_frame(volatile uint8_t *fb, int buffer, const uint32_t *pal, const struct s2_end_in *in, uint32_t fin_frames, uint32_t vf,
                uint32_t press);
