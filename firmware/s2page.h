/*
 * The page runner of Screamer 2's 2D menu pages (docs/screamer2/menus.md): a picture, the page's own drawing over it, the keys with
 * repeat, a fade in and out, and the menu's click sounds. A page is a few functions and a state of its own; the runner is the loop
 * the original runs for every page (0x38d48: keys, sound, step, draw, show). The pause menu (s2pause.c) is such a page.
 *
 * The Pocket's buttons on a page follow Screamer 1's menu: the d-pad is the arrows; B or Start is ENTER, A or Select is ESC.
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "hw.h"

#define S2PAGE_ENTER    (KEY_B | KEY_START)
#define S2PAGE_ESC      (KEY_A | KEY_SELECT)

/* The menu samples of SOUND.DAT's MENU.DTR (s2sound.h). What the original plays differs from page to page; these are the ones read
   from its pages' code (STARTL.EXE 0x41d3c .. 0x41dc1, 0x4228a .. 0x42718): the TRACK page's cursor moves and its ENTER, the
   GAME SETTINGS page's cursor moves (S2PAGE_SND_CURSOR), a value stepped (S2PAGE_SND_ROW) and VIEW HIGH SCORES (S2PAGE_SND_BACK). The arcade
   pages' own are in s2arcade.c. */
enum { S2PAGE_SND_MOVE = 0, S2PAGE_SND_SELECT = 2, S2PAGE_SND_CURSOR = 10, S2PAGE_SND_ROW = 12, S2PAGE_SND_BACK = 13 };

struct s2_page {
    const uint8_t *picture;             /* the 320 x 200 background (an opaque copy), 0: nothing under the drawing */
    const uint32_t *pal;                /* its palette: 256 x 0x00RRGGBB */
    int fade_in, fade_out;              /* frames from black to the picture and back; 0: a cut */
    /* once a frame, before the keys: the page's sound, clocks (tick: frames since the page began, 50 a second); may be 0 */
    void (*frame)(void *self, uint32_t tick);
    /* a press: the buttons that went down in this frame, the d-pad's again while held (after 0.5 s, every 6th frame). Not 0: the page
       ends with that result (after the fade out; a negative result at once, without it: the original's ESC leaves with a cut) */
    int (*key)(void *self, uint32_t press);
    /* the page's commands over the picture (s2gfx.h); may be 0 */
    void (*draw)(void *self, struct blit_list *l, uint32_t tick);
};

/* Runs the page until key() returns a result, and returns it. The button that opened the page does not count as a press. */
int s2page_run(const struct s2_page *p, void *self);

/* The keys with repeat for a loop of your own (the race's pause menu uses the runner; the start menu has its own copy): the state
   starts at the buttons held now */
struct s2_keys { uint32_t before, held; };
void s2page_keys_init(struct s2_keys *k);
uint32_t s2page_press(struct s2_keys *k);
