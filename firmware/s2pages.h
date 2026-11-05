/*
 * The original's GAME SETTINGS and HI SCORES pages (STARTL.EXE 0x4228a and 0x42719, docs/screamer2/menus.md section 6), on the 2D toolkit
 * (s2gfx.h, s2page.h). Each runs until the player leaves it and returns; the options room opens GAME SETTINGS and the MULTIPLAYER MENU (the start menu's two rows open the first and HI SCORES).
 *
 * GAME SETTINGS (BKG_7L picture): the eight settings of the save (s2save.h) and VIEW HIGH SCORES. d-pad up / down move the cursor,
 * B or Start step the row's value round (the original's ENTER; left / right do the same, back and on), A or Select leave at once (VIEW HIGH SCORES fades out). The value
 * under the cursor blinks. Changes go into the save at once. Of the eight, TRANSMISSION, NUMBER OF LAPS, ARCADE ENEMIES SKILL, DAMAGES and
 * MUSIC act in the race (the viewer takes them from the save when a race starts); DETAIL LEVEL, MULTIPLAYER ENEMIES and TIME ATTACK GHOST
 * CAR are kept and nothing more yet.
 *
 * HI SCORES TABLE (HISCOREL picture): the 15 best of a list of the save: TOTAL TIME / LAP TIME, ARCADE / CHAMPIONSHIP / TIME ATTACK,
 * the length of the race and the track, five rows in view. d-pad up / down choose a row (the list scrolls to keep it third), left / right
 * the box above (TOTAL TIME, kind, laps, track, VIEW REPLAY), B or Start step the value in the chosen box, A or Select leave. VIEW REPLAY
 * is lit only for a row that has a replay; there are none yet (the box stays dark).
 */
#pragma once

/* Both need s2gfx_init(). A missing picture leaves the call at once (the log says so). */
void s2_page_settings(void);
void s2_page_hiscores(void);
/* The MULTIPLAYER MENU (STARTL.EXE 0x3bf7a, BKG_6L picture): COMBAT MODE, NETWORK GAME, SERIAL LINK under the title; up / down choose, ENTER (B or Start) answers as the original
   does without a network and a serial link (a message for 5 s under the items), ESC (A or Select) leaves at once. Nothing is played on it. Run by the options room's fourth station. */
void s2_page_multi(void);
