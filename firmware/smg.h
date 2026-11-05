/*
 * The see-through table of Screamer 2, in the 64 rows the hardware has (docs/development.md "Wheel smoke and shadows",
 * docs/architecture.md "Routine 224"; tools/s2/smg_ref.py is the independent reading of these rules and the image check compares the two byte for byte).
 *
 * The game's filler for routines 208 and 224 turns each pixel into SMG[texel * 256 + pixel] and makes that table itself at the start of a race, 112 rows
 * of 256 (STARTL.EXE 0x506c2, 0x50a8e): the palette moved towards asmoke (rows 0 .. 31, texel / 31 of the way), towards black (32 .. 47, the shadow),
 * towards a2smoke (48 .. 79), brighter (80 .. 95) and towards white (96 .. 111). The rasterizer's table has 64 rows and reads six bits of the texel,
 * texel 0 leaves the pixel alone (the geometry unit blends routine 224 only: 208 is a textured face to it). So the image builder takes every
 * see-through texture of its libraries (track, cars, MISC.MLB) and turns it, once, into one the hardware reads as it is:
 *   - every distinct texel 1 .. 111 the textures use gets a row of its own (rows 1 .. 62; row 63 stays the radar's of the Screamer 1 code), in the order of the
 *     texels; with more than 62 the closest pair of one ramp merges (the lower into the higher) until they fit;
 *   - the texture is COPIED with its texels turned into those rows (a texture may be shared with a material that draws its bytes as colours: the lamps'
 *     glow of track 7 and the night tracks are; the copy leaves the original whole), under a texture record of its own, and its material becomes routine 224;
 *   - the table has, for each row, the original's row of that texel.
 * A texel of 112 or more, or of a ramp the race does not make, becomes texel 0 (not drawn). Whoever adds an effect later (lamp glow, headlight cones,
 * the helicopter's rotor) adds its materials to a library the builder passes through here: nothing else.
 */
#pragma once
#include <stdint.h>

#define SMG_FREE    62                  /* rows 1 .. 62 are the see-through texels'; row 0 is never read, 63 is the radar's */
#define SMG_TABLE   16384               /* the table: 64 rows of 256 */

/* the ramps a race makes (bits of ramps) */
enum { SMG_SMOKE = 1, SMG_SHADOW = 2, SMG_SMOKE2 = 4, SMG_BRIGHT = 8, SMG_WHITE = 16 };

struct smg {
    uint8_t seen[32];                   /* bit t: some see-through texture has texel t */
    uint8_t lut[256];                   /* texel -> its row, 0 for none */
    uint8_t alive[SMG_FREE];            /* row r (from 1) -> the original's texel it stands for */
    int rows;                           /* rows used */
    int found;                          /* distinct texels 1 .. 255 the textures have */
    int ramps;
};

/* the ramps a race (race != 0) or a menu's room makes, by TRACK_DATA's asmoke (all three negative: the shadow alone) */
int smg_ramps(const int32_t asmoke[3], int race);

/* The see-through step. front: nfront material records (16 bytes each) of the libraries in front of the MLB; mlb: the MLB section, size bytes so far (a multiple of 256),
   room for cap, whose own ntrack records start it. Every material of routine 208 or 224 (in either) whose texture lies wholly in the section is copied to the end of mlb
   with the texels turned into rows, its record becoming routine 224 on the copy. Returns the new size of mlb (a multiple of 256), 0 if it does not fit. s is
   made ready for smg_table(). */
uint32_t smg_copies(struct smg *s, int ramps, uint8_t *front, uint32_t nfront, uint8_t *mlb, uint32_t ntrack, uint32_t size, uint32_t cap);

/* The rasterizer's table, SMG_TABLE bytes into out: row r is the original's row of texel s->alive[r - 1], from col (256 colours of 3 six-bit values: MAPn.COL) and
   TRACK_DATA's asmoke and a2smoke. Rows 0 and 63 and those not used are 0. */
void smg_table(const struct smg *s, const uint8_t *col, const int32_t asmoke[3], const int32_t a2smoke[3], uint8_t *out);
