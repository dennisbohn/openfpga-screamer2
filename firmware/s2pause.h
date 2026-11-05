/*
 * The race's pause menu (STARTL.EXE 0x49f8e, docs/screamer2/menus.md): the race standing, its picture darkened, and over it the items
 * RESUME, RESTART and EXIT centred in the original's font and spacing, the chosen one bright and blinking, the others dim. The
 * original has a fourth, CD PLAYER, for a machine with a drive; the Pocket has no drive.
 *
 * Buttons (the page runner's, s2page.h): d-pad up / down choose, B or Start take the chosen item, A or Select resume. Start opens the
 * menu, so Start twice is a resume.
 *
 * Sound: the race's effects ease out over a second and come back whole at the end; the music plays on (the original's CD does).
 */
#pragma once
#include <stdint.h>

enum { S2PAUSE_RESUME = 1, S2PAUSE_RESTART, S2PAUSE_EXIT };

/* The picture on show is in framebuffer `front` with the palette `pal` (the race's, 256 x 0x00RRGGBB); the menu replaces it and runs
   until the player has chosen. Needs s2gfx_init(). Returns the item. */
int s2pause_run(int front, const uint32_t *pal);
/* The same with or without RESTART (the championship's race has none: STARTL.EXE 0x49f8e leaves it out, and the two items are centred as n = 2 puts them) */
int s2pause_run_items(int front, const uint32_t *pal, int restart);
