/*
 * What the game keeps between runs, as nonvolatile data slots (core/data.json) in the SDRAM:
 * CHOICE.DAT (slot 23, in Screamer 2's own layout: settings, unlocks, best times; s2save.h) and
 * ATTACK.DAT (slot 24, the time attack's kept runs: a block for each of the seven tracks,
 * s2ghost.h; s2ghost.c reads and writes them in place). save_load() reads them at boot
 * (deferload); from then on the CPU's writes there are the save: the APF reads both back over
 * the bridge when the core exits or the Pocket is switched off, as it does debug.txt.
 */
#pragma once
#include <stdint.h>

#define SAVE_CHOICE_SLOT    23
#define SAVE_CHOICE_ADDR    0x13F00000u     /* S2SAVE_SIZE bytes (s2save.h), 64 KB of window up to ATTACK.DAT */
#define SAVE_ATTACK_SLOT    24
#define SAVE_ATTACK_ADDR    0x13F10000u     /* past the program's 15 MB (link_prog.ld) */
#define SAVE_ATTACK_SIZE    360096u         /* the slot's size (core/data.json); s2ghost.h's seven blocks take 360,080 of it; all zeros until time attack keeps a run */

/* Both slots from the SD card into the SDRAM; a missing file, a failed read or a CHOICE.DAT
   that is not ours is replaced by the defaults (s2save_load(); zeros for ATTACK.DAT). Then each
   slot's full size into the APF's slot table, so the APF saves it on exit (it keeps a size only
   for a file it found). Early: until then, an exit would save what lies in the SDRAM there. */
void save_load(void);
