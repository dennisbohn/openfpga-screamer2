/*
 * The race screen of Screamer 2 in the chase camera (STARTL.EXE 0x45a4e, the instrument 0x46d42, the bar 0x458c7, the bends' arrows
 * 0x456c8, the leader's mark 0x4962c; docs/screamer2/hud.md): the original's own sprites and fonts (GAMEL.SPR, GAMEL.FNT of GFX/GAME1.DAT, loaded
 * by s2gfx_game_init) drawn by the blitter over the finished scene, at the original's places, from the race of race.h:
 *   the lap (LAP, the lap, the laps), the place (POSITION, the place, the laurel for the leader), this lap and the total time, the track's best lap
 *   and total time, the bar of the four cars along the track, the checkpoint gap (+00:01:23), BEST LAP!, WRONG WAY, the bends' arrows, the start
 *   lights (3, 2, 1, GO), the dial with the speed, the gear and the rev needle, and the 1ST mark over the car that leads.
 * Everything is one blitter list (at most about 100 commands) handed on at once; the numbers are formatted here, no library calls.
 */
#pragma once
#include <stdint.h>

struct zip;
struct track_cam;
struct su_view;

/* Once, after the ZIP is open (and s2gfx_init(), which reads the texts of DATA/LOCAL.DAT): the sprites and fonts and the texts (THIS LAP, TOTAL,
   BEST LAP, BEST TOTAL, BEST LAP!, WRONG WAY, POSITION, LAP, MPH). 1 if the screen can be drawn; the log says what is missing. */
int s2hud_init(struct zip *z);
int s2hud_ready(void);

/* A race starts on track 1 .. 7 (race_init() has run): the track's best lap and total time for race_laps laps (s2_best_get), the first
   checkpoint, the bends' arrows (MAPnDIR.SPL of DATA/MAPn.DAT), every timer and the dial at rest. */
void s2hud_begin(int track);

/* One frame: the race's state (race_car(), race_get_hud()) as of now, `steps` of 20 ms since the last frame (the timers, the dial's easing),
   cam and view the scene was drawn with (the leader's mark is placed over the car as drawn). top: rows to move the elements near the top left
   down by (0, or 16 where the timing overlay's two lines are), so that both can be read. Builds the blitter list for the back buffer and
   hands it to the blitter, which draws it when the 3D units are done; the caller waits for the blitter (RS_DRAW & RS_BUSY) before the swap. */
void s2hud_draw(const struct track_cam *cam, const struct su_view *view, uint32_t steps, int top);
