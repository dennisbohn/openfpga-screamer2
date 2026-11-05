/*
 * The 2D toolkit of Screamer 2's menu pages, the loading screen and the pause (docs/screamer2/menus.md): the original's pictures
 * (.BKK), sprites (MENUL.SPR) and fonts (MENUL.FNT) from GFX/GRAF1.DAT of the ZIP, drawn by the blitter, and the darkened still
 * of the pause. Everything is drawn into the back buffer as one list of blitter commands per frame (blit.h) and swapped in.
 *
 * SDRAM (docs/soc.md; Screamer 1's menu region, free in Screamer 2), S2G_MEM = 0x10A0_0000:
 *   +0x00000  picture 0      64,000 bytes   the page's picture (s2gfx_bkk)
 *   +0x10000  picture 1      64,000 bytes   a second one, or the pause's still
 *   +0x20000  stage          160 KB         an entry's stored bytes, read by range from the ZIP
 *   +0x48000  unpack         72 KB          an entry unpacked (a .BKK is 64,792 bytes, MENUL.FNT 69,748)
 *   +0x60000  sprites, fonts 0.57 MB        MENUL.SPR as the file is, then the fonts' glyphs one byte a pixel
 * A deflated ZIP cannot be read by range: its GRAF1.DAT (4.2 MB) is inflated once, whole, at 0x1100_0000 (the files screen's read
 * test region, free by then).
 *   +0x100000 race sprites   0.2 MB         GAME1.DAT's GAMEL.SPR as the file is (the race screen's track banners, s2gfx_banner)
 * The race screen's set (s2gfx_game_init) has its own 0.5 MB at 0x10C0_0000 (GAMEL.SPR as the file is, 0x40000 of it, then the glyphs).
 */
#pragma once
#include <stdint.h>
#include "blit.h"

struct zip;

/* The fonts of MENUL.FNT, by the number the original's text routine takes: 0 the titles (29 pixels), 1 the 18-pixel one (the
   race screen's font 0 too: GAMEL.FNT's is the same art), 2 and 3 the small (12), 4 a second 18, 5 the small digits (7) */
enum { S2G_FONT_TITLE, S2G_FONT_MEDIUM, S2G_FONT_SMALL, S2G_FONT_SMALL2, S2G_FONT_MEDIUM2, S2G_FONT_DIGITS };

/* A colour of the fonts is a ramp of 15 palette entries, colour * 16 + shade (shades 1..15): 0 grey / orange, 1 white, 2 gold, in
   every BKK's palette and every track's */
#define S2G_WHITE   1

#define S2G_PIC_BYTES   (320 * 200)
#define S2G_PALETTE     256

/* Once, after the ZIP is open: GRAF1.DAT's directory, MENUL.SPR and MENUL.FNT into the tables, the texts (LOADING..., RESUME, ...)
   of DATA/LOCAL.DAT. 1 if the pictures, sprites and fonts are there (the log says what is not). */
int s2gfx_init(struct zip *z);

/* the ZIP s2gfx_init() was given (the 3D pages read their containers from it) */
struct zip *s2gfx_zip(void);

/* 1 once s2gfx_init() has found the pictures, the sprites and the fonts (the menu pages are not shown without them) */
int s2gfx_ready(void);

/* The picture buffers (S2G_PIC_BYTES, 4-aligned, in the SDRAM) */
uint8_t *s2gfx_pic(int k);

/* GFX/GRAF1.DAT's name (e.g. "LOAD_1L.BKK") into dst (a picture buffer) and its palette into pal (256 x 0x00RRGGBB, the 6-bit values
   times 4 as the track's palette has them). A stored ZIP is read by range (about 40 KB for a picture) and unpacked (65 KB). 1, or 0
   if the file is not there or damaged. */
int s2gfx_bkk(const char *name, uint8_t *dst, uint32_t *pal);

/* A line of DATA/LOCAL.DAT's STRINGS.TXT or GAME.TXT: the one k lines after the line `key` (comments (*) left out), or `fallback`
   (the built-in English text, which is the same) if there is none */
const char *s2gfx_line(const char *key, int k, const char *fallback);

/* One frame: wait for the last swap, then the blitter list for the back buffer (reset); the page adds its pictures, sprites and text,
   and s2gfx_end() hands the list to the blitter, sets the back buffer's palette (pal at fade of 0x8000, pal_faded) and swaps. */
struct blit_list *s2gfx_begin(void);
void s2gfx_end(const uint32_t *pal, int32_t fade);

/* The same for a frame somebody else is drawing into framebuffer `buffer` (the race's back buffer, once its rasterizer is done), and the
   end of it: the list to the blitter, waited for, the boxes of s2gfx_dark_after() applied. No palette, no swap. */
struct blit_list *s2gfx_begin_on(int buffer);
void s2gfx_flush(void);

/* The whole screen from a picture buffer (opaque copy) */
void s2gfx_picture(struct blit_list *l, const uint8_t *pic);
/* Sprite n of MENUL.SPR with its own offset: its pixels start at (x + its x, y + its y) */
void s2gfx_sprite(struct blit_list *l, int n, int x, int y);
/* Sprite n in the middle of the screen at row y (the original's 0x333d9: (320 - (w + x offset)) / 2 - x offset / 2) */
void s2gfx_sprite_centred(struct blit_list *l, int n, int y);
/* Text as the original's engine routine with an explicit spacing (0xc67f0 draws, 0xc6c88 measures): `space` pixels between two characters (none
   before the first or after the last); a control byte 1 sets fixed pitch (a character centred in a cell of the font's widest, the first's cell
   without the space, the later ones with it), 2 proportional. Draws at (x, y) and returns the width in pixels; with l = 0 it only measures. */
int s2gfx_text(struct blit_list *l, int font, int colour, int space, int x, int y, const char *s);
/* The same centred on the screen (0x3327c: x = (320 - width) / 2, the width as the original measures it, 0xc6c88: the characters and the gaps
   between them, none after the last; checked against the running original on the ARCADE page) */
void s2gfx_text_centred(struct blit_list *l, int font, int colour, int space, int y, const char *s);

/* The same centred in the box x0 .. x0 + width (0x332f5: x = x0 + (width - text width) / 2, the width measured as s2gfx_text_centred does) */
void s2gfx_text_boxed(struct blit_list *l, int font, int colour, int space, int x0, int width, int y, const char *s);

/* The clip of the original's drawing (0x51189 for text, 0x50f3c for sprites: a box of rows, whole width): what s2gfx_text and
   s2gfx_sprite draw outside the rows [y0, y1) is cut off. (0, 200, 0, 200) for none, the state after s2gfx_init(). */
void s2gfx_clip(int text_y0, int text_y1, int sprite_y0, int sprite_y1);

/* MENUL.SPR's cars' side views: sprite S2G_SPR_CAR + 5 * team + car, team 0 .. 3 and car 0 .. 4 (31 x 10 to 12 pixels). They are
   the same pictures as the race screen's (GAMEL.SPR 0x2e ..). */
#define S2G_SPR_CAR 0x55

/* The track banner of the race screen (GAMEL.SPR 0x27 + track - 1, 144 x 50: the end of the race) as a sprite number for
   s2gfx_sprite(); track 1 .. 7. The art is the loading screen's but for COLUMBIA, which the loading screen spells COLOMBIA. Without
   GAMEL.SPR (the ZIP has no GAME1.DAT) the loading screen's banner. */
int s2gfx_banner(int track);

/* The race screen's sprites and fonts: GAMEL.SPR and GAMEL.FNT of GFX/GAME1.DAT (the formats of MENUL's; 115 sprites and 9 fonts, font 0 is the menu's
   font 1: docs/screamer2/menus.md). Its own tables and memory (SDRAM_BASE + 0xC00000, 0.5 MB), independent of s2gfx_init(); the calls are the menu's
   with the same meaning. A stored ZIP is read by range, a deflated one inflates GAME1.DAT (0.45 MB) at SDRAM_BASE + 0x1500000 for the read only.
   1 if the sprites and fonts are there (the log says what is not). */
#define S2G_GAME_MEM    0xC00000u
int s2gfx_game_init(struct zip *z);
void s2gfx_game_sprite(struct blit_list *l, int n, int x, int y);
void s2gfx_game_sprite_centred(struct blit_list *l, int n, int y);
int s2gfx_game_text(struct blit_list *l, int font, int colour, int space, int x, int y, const char *s);
void s2gfx_game_text_centred(struct blit_list *l, int font, int colour, int space, int y, const char *s);

/* The darkening of the original (it builds a table for the palette every picture is darkened with): for each colour the palette entry
   nearest to 0.67 of it (the first of equals), and where that is the colour itself, the nearest to 0.7 of that again. Entries from
   `keep` up to 255 are never the result (a few the page wants for itself); 256 for none. */
void s2gfx_dark_table(const uint32_t *pal, uint8_t table[256], int keep);
/* The screen in framebuffer `buffer` (the one on show) into dst through the table, or as it is with table 0 */
void s2gfx_snapshot(int buffer, uint8_t *dst, const uint8_t *table);
/* The box of a picture through the table */
void s2gfx_dark_box(uint8_t *pic, int x, int y, int w, int h, const uint8_t table[256]);
/* The box of a framebuffer through the table, as the original's 0x51868 does it: in words of four pixels, so x and w lose their low two
   bits (a box at x 30, 260 wide, is 28 .. 288) */
void s2gfx_dark_fb(volatile uint8_t *fb, int x, int y, int w, int h, const uint8_t table[256]);
/* The same box, `passes` times, applied to the frame being built once the blitter has drawn it (s2gfx_end), for the page's text under it
   too: up to 4 boxes a frame, forgotten by s2gfx_begin(). */
void s2gfx_dark_after(int x, int y, int w, int h, int passes, const uint8_t *table);
