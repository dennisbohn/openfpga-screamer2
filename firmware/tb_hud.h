/*
 * A .PKC picture of the game cut into boxes around its pixels for the blitter, as
 * tools/make_track.py's pkc() cuts a picture (tb_hud.c says how).
 */
#pragma once
#include <stdint.h>

/* One .PKC picture as boxes into out (cap bytes): u32 count; per box u32 offset (from out),
   u16 width, height, int16 x, y; the boxes' pixels. Returns the size, 0 if the file is
   malformed or the boxes do not fit. (The cockpit's dashboard, s2dash.c, at the race's start.) */
uint32_t tb_hud_picture(const uint8_t *pkc, uint32_t size, uint8_t *out, uint32_t cap);
