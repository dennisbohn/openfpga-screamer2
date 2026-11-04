/*
 * The hardware path of a frame (main.c has it from Screamer 1's race; the viewer, viewer.c, uses it as
 * it is): pieces from the CPU into the geometry unit, the blitter's lists, the frame's timeline for
 * debug.txt, the track's palette into the back buffer.
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "geom.h"
#include "setup.h"

/* the back buffer the hardware draws into, the cycles the DRAW writes waited for the rasterizer, and the
   backdrop's blitter commands, which the first piece waits for (back_wait) */
extern uint32_t draw_buf, draw_wait;
extern int back_wait;
extern uint32_t back_clocks, back_cycles;

/* A piece (track_hw_piece) into the geometry unit's buffer; a blitter list to the blitter. If hw_idle is set and handing
   the piece on had to wait HW_FULL_CYCLES or more for room in the buffer (it is full: the geometry unit has 100 pieces
   to do, and the piece it has just finished took that long), hw_piece calls it once: other work of the CPU for the time
   it would wait (the viewer's computer cars' steps). A piece handed on without waiting takes about 100 cycles; shorter
   waits happen early in a frame, when the geometry unit is waiting for the CPU, and there the work would hold it up */
void hw_piece(const struct gu_piece *g);
extern void (*hw_idle)(void);
#define HW_FULL_CYCLES  1200
void hw_blit(const struct blit_list *l);
/* The projection into the setup unit's registers */
void su_view_regs(const struct su_view *v);

/* The frame's timeline (tools/make_timeline_svg.py draws it): at each mark the cycles since the frame
   began, the busy clocks of the units and the CPU's cycles writing to them. tl_begin() with the cycle
   counter at the frame's start, tl_mark() at each stage, tl_log() the last whole frame's marks into
   the debug log. hw_piece() marks the pieces while tl_scene is set. */
enum { TL_START, TL_PHYS, TL_PREP, TL_CULL, TL_PIECES, TL_SCENE, TL_CLEAR, TL_HUD, TL_WAIT, TL_END, TL_MUSIC };
extern uint32_t tl_scene;
void tl_begin(uint32_t t_frame);
void tl_mark(uint32_t tag);
void tl_log(void);

/* The palette entry nearest to an RGB colour; pal at f of 0x8000 into the back buffer's palette */
uint8_t nearest(const volatile uint32_t *pal, int r, int g, int b);
void pal_faded(const uint32_t *pal, int32_t f);

/* The stack the program used so far (bytes); the frame count the trap handler reports */
uint32_t stack_used(void);
extern uint32_t frames_shown;
void log_head(const char *what, uint32_t n);
