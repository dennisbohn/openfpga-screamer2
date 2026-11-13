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

/* Combat mode's frame (two players on the screen's halves, race.h race_combat) in s2hud_draw()'s place: the race screen of car 0 in the upper half and of car 1 in the lower (the lap,
   the place, the bar, the bend's arrow, WRONG WAY, the dial; none of the single screen's words and clocks), the start lights once over both, rows 99 and 199 black. */
void s2hud_split(uint32_t steps, int top);
/* The view the scene is drawn in (s2cam.h: 1 the trackside cameras, 3 the chase camera, the default) and the car it follows: in view 1 the race screen shows no dial and none of
   its texts or WRONG WAY (0x45d17, 0x463fc, 0x46fea), only the leader's mark, the start lights and the replay's band; the dial is that of the car followed. Reset by s2hud_begin(). */
void s2hud_view(int view, int follow);
/* The views from the car (s2cockpit.h): the rear-view mirror's glass if it is on (S2CK_MIRROR_W x S2CK_MIRROR_ROWS pixels, 4-aligned, in the SDRAM; it goes over the scene and the leader's
   mark with its frame, MIRROR_L.PKC, in every view but the trackside cameras; 0: off) and the scene's rows (200, the dashboard's in INSIDE: the leader's mark is cut there). In the view
   INSIDE (S2CK_INSIDE) the cockpit's picture, gear lamp and two needles take the dial's place. Reset by s2hud_begin(). */
void s2hud_scene(const uint8_t *mirror, int rows);

/* The replay's band (0x4658c, while the player's car is still racing): the bottom 20 rows darkened and `text` ("TEAM - VIEW") in the big letters at (10, 180). Drawn after the
   rasterizer is done, into the framebuffer fb (buffer is its number) as the end screen is; s2hud_band_prepare() makes the darkening table of the race's palette (70 ms
   on the Pocket: once, when the replay begins). */
void s2hud_band_prepare(const uint32_t *pal);
void s2hud_band(volatile uint8_t *fb, int buffer, const char *text);
