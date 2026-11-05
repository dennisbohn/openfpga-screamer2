/*
 * The cars' windows' reflection picture of Screamer 2 (docs/screamer2/comparison.md E10, docs/development.md "The windows").
 *
 * The game draws a car's glass (routine 160, a material of 8 x 8 with a placeholder picture in the car's .MLB) with the first material of an
 * ENV.MLB: the race's `GFX/ENVP<track><look>1.DAT`, the menu pages' `GFX/ENVM<team>1.DAT` (STARTL.EXE 0x2ee15, 0x44afa: the page that shows the
 * team's cars loads the team's). That material is a 128 x 128 picture of 1 to 3 frames; the filler masks a texel's address with 0x7f7f, where the
 * hardware wraps routine 160's u, v at 256 (and a window's u, v are whole texels). So each frame is stored tiled 2 x 2 as 256 x 256, under a texture
 * record of its own in the merged .MLB, and the cars' routine-160 materials point to it: no hardware change. The picture's pixels are palette
 * indices of the page that draws them (the page's palette, not ENV.COL's: the original's CAR page shows them through its own).
 *
 * The same code builds the race image's reflection (track_build.c) and a menu page's cars' (s2showroom.c).
 */
#pragma once
#include <stdint.h>

#define S2_ENV_HEAD  256u               /* the reflection's texture record, padded to this */
#define S2_ENV_FRAME 65536u             /* a tiled frame: 256 rows of 256 */
#define S2_ENV_MAX   8u

/* the bytes of a reflection block of n frames: the texture record and the tiled frames */
#define S2_ENV_BYTES(n) (S2_ENV_HEAD + S2_ENV_FRAME * (n))

/* The material records of an .MLB: the table of 16-byte records runs up to the first texture record */
uint32_t s2_mlb_records(const uint8_t *m, uint32_t size);

/* 1 if a material's texture record (its word +4, an offset in the .MLB) lies inside the file */
int s2_mlb_texture(int32_t ro, uint32_t size);

/* 1 if the 16-byte material record is a window: routine 160, the reflection */
int s2_env_window(const uint8_t *rec);

/* ENV.MLB (size bytes): its first material is routine 160, 128 x 128; its texture record is n (1 to 3 frames), a word, the frames' offsets; a
   frame is 128 rows of 256 bytes with the picture in the first 128 columns. Returns n, 0 if it is not that */
uint32_t s2_env_frames(const uint8_t *env_mlb, uint32_t size);

/* The reflection's block of ENV.MLB (n frames, checked by s2_env_frames) at dst (S2_ENV_BYTES(n) bytes), to lie at offset `at` of the .MLB the
   windows are drawn with: the texture record (n, the ENV record's word +4, the frames' offsets), then each frame tiled 2 x 2 */
void s2_env_write(uint8_t *dst, const uint8_t *env_mlb, uint32_t n, uint32_t at);

/* The picture of the reflection a car's windows show by the kind of the ground under its fourth wheel (STARTL.EXE 0x1f1ee, the chain of compares at 0x1f372: each
   case stores the phase into the car's +0xc80, which 0x1737b copies into the object's +0x3c, and for the player's car the rate into the constant at 0xb0678): the
   phase (the frame of an ENV.MLB of n frames is n x phase >> 16; geom.h) and, in *rate if it is not 0, how fast the reflection slides across the glass as the car
   moves, 0.03 where the picture changes (kinds 7, 13, 34, 43) and on 44, else 0.002. Kinds 7 and 34 phase 0x8000, 13 0x5556, 43 0xaaaa, all the others 0. */
uint32_t s2_env_phase(int kind, float *rate);

/* A car's .MLB (size bytes, in place) with its windows' materials pointing to the texture record at offset env_at of the file */
void s2_env_windows(uint8_t *mlb, uint32_t size, uint32_t env_at);
