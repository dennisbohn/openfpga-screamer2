/*
 * The viewer: milestone 2 of Screamer 2 on the Pocket. A track of the game, built by the firmware from
 * the ZIP (track_build_s2) and drawn through the hardware path (geometry unit, setup unit, rasterizer,
 * blitter) with the original's level of detail at DETAIL HIGH, from the chase camera over a car that
 * drives the road at 150 km/h in a loop (road.h), with the player's car and three more ahead of it.
 * No menu, physics, race screen, sound or music: it shows what the renderer does with the game's own
 * data, and how long a frame takes (docs/screamer2/performance.md has the forecast).
 *
 *   d-pad left / right   the previous / next track (1 to 7; the track is loaded again)
 *   A                    the cars: 4 (the realistic field), 0, 1 (the player's) in turn
 *   B                    the timing overlay on / off
 * The overlay (top left, drawn after the 3D picture): the mean and the worst time of a frame over the
 * last second, from the frame's start to the end of the rasterizer's work (not the wait for the next
 * vertical blanking), the track and the cars. The first line of debug.txt is the same, live; a line goes
 * into the log (4 KB: kept short) 5 s after a track is shown, after 30 s, then every minute, and one when it is
 * left; a track's loading is one line.
 */
#pragma once
#include <stdint.h>

/* Start at track 1 (in the simulation: the track the testbench names) and never return. results: the
   boot loader's results, for the simulation's end. Needs the ZIP opened by files_check() and the read
   test passed (files_ready()). */
void __attribute__((noreturn)) viewer_run(uint32_t results);
