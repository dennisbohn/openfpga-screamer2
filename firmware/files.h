/*
 * The game's files that the core reads from Assets/screamer2/common/ on the SD card: one ZIP of
 * the original DATA, GFX and ANIMS folders (SCREAMER2.zip, data slot 3, zip.h) and the music
 * (GAME.GOG and its cue sheet GAME.DAT, slots 22 and 21), core/data.json. The screen lists them
 * with their state, says in plain words what is wrong with the ZIP, and warns before anything is
 * loaded if the ZIP is compressed (deflated entries load slowly, stored ones fast).
 */
#pragma once
#include <stdint.h>
#include "zip.h"

/* Open the ZIP (scratch: ZIP_SCRATCH bytes of the SDRAM, free afterwards), look for every file
   and return how many of the files a race needs are missing or cannot be read (a method other
   than stored or deflated). The log gets "SCREAMER2.zip: ...", "zip: needed files: n stored, n
   deflated, ..." and "game files: n missing: ..." (the missing files, at most 12 names). Call
   before files_draw. */
int files_check(uint8_t *scratch, uint32_t scratch_size);

/* Draw the list into fb: the ZIP, then every file in green (there), red (needed, missing) or
   grey (optional, missing), and what to do, or "ready" (with the notice when the ZIP is
   compressed, and the result of the read test). failed: the files are there, but loading them
   failed (the log says why) */
void files_draw(volatile uint8_t *fb, int failed);

/* The read test, when nothing is missing: some of the needed files (the first race's) are read
   whole into dest (max bytes), one after the other, each logged as "zip: <name> method <m> <bytes>
   bytes in <ms> ms (<cycles> cycles)", which shows the real speed of a stored or deflated entry;
   a file above limit bytes is skipped. The screen shows the result on its next drawing. Call it
   once the first picture is up, so the notice for a compressed ZIP is on screen while it runs. */
void files_load_check(uint8_t *dest, uint32_t max, uint32_t limit);

/* After files_check() and files_load_check(): 1 if nothing a race needs is missing and the read test
   passed (the viewer may go on); the ZIP the screen opened, for the viewer to read from */
int files_ready(void);
struct zip *files_zip(void);
