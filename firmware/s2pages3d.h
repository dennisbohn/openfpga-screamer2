/*
 * TEAM SELECTION and ENTER YOUR NAME of Screamer 2's menu (docs/screamer2/menus.md, row 5): one page of the original in two states
 * (STARTL.EXE 0x3ffab .. 0x41a60, flag 0x1324c4 0 / 1), 3D objects over the S2 watermark picture (LOGOL.BKK) on the 3D-o toolkit (s2scene.h).
 *
 *   TEAM SELECTION   the four teams' badges (GFX/LOGO1.DAT A1LOG-00 .. A4LOG-00.3DX, material library LOGO.MLB) turn, the chosen one in front at half
 *                    a turn a second (20 of 2048 a frame), the others at an eighth, on slots left, above and right behind it; LEFT / RIGHT choose (the badges
 *                    glide to their new slots), ENTER goes on to the name, ESC leaves (the original asks EXIT SCREAMER 2? here: a core cannot end itself)
 *   ENTER YOUR NAME  the badges fly off (the chosen one up and a little back), 26 letter tyres (ALETTER.3DX, one frame of LOGO.MLB each) fly in on a ring
 *                    round the middle, the chosen letter at the front tilted to the camera; LEFT / RIGHT turn the ring (the letter becomes the name's
 *                    letter at the cursor), UP / DOWN move the cursor along the three letters, ENTER keeps the name and leaves (a flash to white),
 *                    ESC goes back to the team
 *
 * The Pocket's buttons: B or Start is ENTER, A or Select is ESC. The team and the name go into the choices (s2menu.h) and the save (s2save.h:
 * S2S_TEAM, the name, and S2S_PROFILE = 1, which a first start looks at).
 */
#pragma once
#include <stdint.h>
#include "s2menu.h"

/* what the page ends with (S2P3D_IDLE: 30 s on TEAM SELECTION with no button down, the original's rule: the attract sequence follows, s2attract.h) */
enum { S2P3D_FAIL = 0, S2P3D_DONE = 1, S2P3D_IDLE = 2, S2P3D_QUIT = -1 };

/* The page until the name is kept (S2P3D_DONE: c->team and the save's name and profile are set) or the player presses ESC
   (S2P3D_QUIT: nothing is changed, the start menu comes). S2P3D_FAIL if the files could not be read (nothing is shown). Needs s2gfx_init(). */
int s2_page_team(struct s2_menu_cfg *c);
/* TEAM SELECTION alone, for a player of combat mode (s2page_player says whose: PLAYER n under the title): ENTER keeps the team in c->team (S2P3D_DONE) and nothing goes into the
   save, there is no name and the page does not time out; ESC is S2P3D_QUIT. */
int s2_page_team_pick(struct s2_menu_cfg *c);
