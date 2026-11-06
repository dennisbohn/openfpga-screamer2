/*
 * The attract sequence of Screamer 2 (docs/screamer2/menus.md, row 9; STARTL.EXE 0x3ca20 the page, 0x37ec1 its screen): when the menu has been left alone for 30 s the game
 * runs a race of four computer cars on a track (every start the next of England, California, Finland, Switzerland, Columbia, Egypt: the table at 0x113224) under trackside
 * cameras, and draws over it, instead of the race screen, the logos and the credits of ATTRACT.TXT:
 *
 *   stage 0      one black frame
 *   stages 1, 2  a black screen; the Virgin logo (GAMEL.SPR 0x6e) and then the Milestone logo (0x6f) slide in from the right (x 1000 and 640, eased by the table's row 0x55,
 *                0.925 a step) to the middle, at y 10; once within 5 pixels of it the line under it (ATTRACT.TXT line 0 "AND", line 1 "PRESENTS", y 0xa5) shows and,
 *                3 s later, the next stage begins
 *   stage 3      the race out of white (the palette from white over 1 / 1.2 s) under the SCREAMER 2 title (0x71, y 4); the track's song begins here
 *   stage 4      the credits: the lines up to the next '#' of ATTRACT.TXT, one a 16 rows from y 100, for 4 s, then a second of nothing, group after group; a group with no
 *                line (the file's end) ends it
 *   the end      a press (the original's ENTER or ESC; on the Pocket any button) or the end of the credits: at once before stage 3, else the palette goes to black in
 *                half a second; the last frame is black with the big S2 logo (0x70, y 40), which stays while the next page loads
 *
 * s2att_step() is the screen of one 20 ms step as the original's routine runs it for one frame at 50 frames a second (its numbers are the program's, each in the order and the
 * precision the code does it); the viewer calls it for each step a frame takes. Nothing here touches the hardware but s2att_draw(), which only adds blitter commands.
 */
#pragma once
#include <stdint.h>
#include "blit.h"

struct zip;

/* GAMEL.SPR's sprites of the sequence */
enum { S2ATT_SPR_VIRGIN = 0x6e, S2ATT_SPR_MILESTONE = 0x6f, S2ATT_SPR_S2 = 0x70, S2ATT_SPR_TITLE = 0x71 };

#define S2ATT_LINES 160                 /* lines of ATTRACT.TXT kept (the file has 105) */
#define S2ATT_IDLE  30.0f               /* seconds of the menu left alone before the sequence starts (TEAM SELECTION, 0x40236: the float 30.0) */

struct s2att {
    int stage;                          /* 0 .. 5 (0x127ecc) */
    int group;                          /* the line the credits' group on show begins at (0x128890) */
    float x;                            /* the logo's place (0x128888) */
    float shown;                        /* seconds the logo's line has been up / the white's level / the group's fade (0x127ed0) */
    float gap;                          /* the second between two groups (0x127ed4) */
    float black;                        /* the palette's way to black at the end (0x13249c) */
    float lit;                          /* the last white level the palette was set with (stage 3), which stays until the way to black begins */
    int leave;                          /* a press, or the end of the credits (0x132498) */
    int over;                           /* the race is over (0x11f0a8 |= 3) */
    int cued;                           /* the song has been let go (0x11f09c) */
    int n;                              /* lines read */
    const char *line[S2ATT_LINES];
};

/* What the frame shows, in the order it is drawn */
struct s2att_view {
    int black;                          /* the screen is cleared to black first */
    int sprite, sx;                     /* GAMEL.SPR sprite `sprite` with its place at (sx, 10), -1 for none */
    int caption;                        /* line index drawn centred at y 0xa5, -1 for none */
    int title;                          /* the SCREAMER 2 title (0x71) centred at y 4 */
    int first, count;                   /* credits: lines [first, first + count) centred at y 100 + 16 k */
    float white;                        /* the palette towards white by this share (the picture comes out of white; what the last frame of that left it at stays) */
    float dark;                         /* the palette (the race's own, not the one above) towards black by this level */
    int exit;                           /* the last frame: black with the S2 logo (0x70) centred at y 40, the palette as it is */
    int music;                          /* the song begins with this frame */
};

/* ATTRACT.TXT of DATA/LOCAL.DAT in the ZIP (the whole container is read: 2.6 KB) into a. Empty lines count (the credits' groups are laid out with them). The text stays in
   a buffer of its own. 1 if the file is there and has the lines (the log says if not). Needs s2gfx_init() (its picture buffer is the stage the container is read through). */
int s2att_load(struct s2att *a, struct zip *z);
/* The same from the text of the file (the tests) */
int s2att_text(struct s2att *a, const char *text);

/* The sequence begins (stage 0, no frame yet) */
void s2att_begin(struct s2att *a);
/* One step of 20 ms: `press` if a button went down in it, the places of the logos from the sprites' table (s2gfx_game_sprite_info). a->over is set in the frame the race ends. */
void s2att_step(struct s2att *a, int press, struct s2att_view *v);
/* the view's sprites and text into the list (the race screen's sprites and fonts, s2gfx_game_init()) */
void s2att_draw(struct blit_list *l, const struct s2att *a, const struct s2att_view *v);
/* the palette `pal` as the view shows it: towards black by v->dark if that is set, else towards white by v->white (0x514c1 with 255, 255, 255: the 6-bit entries
   (c - 63) x (1 - level) + 63, cut; with 0, 0, 0: c x (1 - level)). 0 if nothing of the sort (pal is shown as it is), else 1 with the colours in out */
int s2att_palette(const uint32_t *pal, const struct s2att_view *v, uint32_t out[256]);

/* The track of the next sequence, 1 .. 7 (the table 0x113224 gives the game's 0, 2, 3, 4, 5, 1: England, California, Finland, Switzerland, Columbia, Egypt) and the
   counter that moves on after each sequence, however it ended (0x3cbab; the original keeps it in CHOICE.DAT, this one until the power goes) */
int s2att_track(void);
void s2att_next_track(void);

/* The idle timer of a page (inline: the pages need no more of this file): `t` is set to S2ATT_IDLE by s2att_idle_reset() and, in every step of 20 ms the page decides is idle (no button
   down), takes the frame's time off (0x40847: t - dt stored as a float); s2att_idle_step() is 1 once it is below 0 */
static inline void s2att_idle_reset(float *t) { *t = S2ATT_IDLE; }
static inline int s2att_idle_step(float *t)
{
    *t -= 0.02f;
    return *t < 0.0f;
}
