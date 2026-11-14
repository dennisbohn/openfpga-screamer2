/*
 * DEBUG selects the development code. It is 1 in a build for development (tools/fw.sh DEBUG=1, or make
 * DEBUG=1; tools/fw.sh also sets it for its measuring variants PROF=1 and TAG=...) and 0 in a release,
 * which is the default.
 *
 * A release leaves out what was built to measure and to test:
 *   - the timing overlay (L and R together in a race) and the statistics behind it
 *   - debug.txt's live first line, the visits' statistics lines, the late frames' lines and the record of
 *     the player's buttons in a race (the simulation's replay is made from it)
 *   - the shortcut to the championship's desk scenes (L and R held on LEAGUE SELECTION)
 *   - the viewer's ride and its buttons on the error screen (a race that cannot start goes back to the menu)
 *   - everything open from the start: the Makefile gives a release S2SAVE_UNLOCK_ALL=0, the original's
 *     gating (s2save.h), and a debug build 1
 * A release keeps what a player or a bug report needs: the boot log, the loading of the files and their
 * list, the menu's and the championship's lines, the faults, and the simulation's own hooks, which run only
 * in the simulation. The frame's timeline marks (frame.h) stay as well: they cost microseconds, and the hot
 * code's place in the boot RAM was measured with them (docs/screamer2/performance.md).
 *
 * The code is switched by if (DEBUG), not left out by the preprocessor, so both builds compile all of it.
 */
#pragma once

#ifndef DEBUG
#define DEBUG 0
#endif
