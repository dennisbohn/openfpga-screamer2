/*
 * The 3D-o pages of Screamer 2's menu (docs/screamer2/menus.md, row 5): a few meshes of the game through the geometry unit over a 320 x 200
 * picture, then the page's sprites and text. The meshes (.3DX) and material libraries (.MLB) are the game's own files, read from the
 * containers of the ZIP and used as they are (the page's material file is the unit's GU_MLB, as Screamer 1's menu had it); the pieces are made
 * by track.c (track_block_piece: the race's lens) and handed on by hw_piece().
 *
 * SDRAM (docs/soc.md), S3D_MEM = 0x1180_0000, 4 MB up to the sound effects at 0x11C0_0000, free in the menus (the files screen's read test
 * sits below it at 0x1100_0000, a deflated GRAF1.DAT's inflate at 0x1100_0000 .. 0x1143_0000):
 *   +0x000000  stage   256 KB   the stored bytes of one container entry, read by range from a stored ZIP
 *   +0x040000  whole   1 MB     a deflated ZIP's container, inflated whole (it cannot be read by range)
 *   +0x140000  models  2.75 MB  the page's meshes and libraries: TEAM SELECTION / ENTER YOUR NAME 0.65 MB, the CAR page's five cars 1.1 MB
 * Nothing of the race lives there; the track image (0x1210_0000), the cars of the grid (0x12D6_0000) and the toolkit (0x10A0_0000) are not touched.
 *
 * The game's coordinates here are the engine's: x to the right, y up, z towards the viewer (the camera at z > 0 looks along -z); angles are
 * 2048ths of a turn. world = rotate(local): about x by a0 (+y turns towards +z), then about y by a1 (+x turns towards +z).
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "track.h"
#include "zip.h"

#define S3D_MEM         0x1800000u              /* SDRAM_BASE + this */
#define S3D_STAGE_MAX   0x40000u
#define S3D_WHOLE_MAX   0x100000u
#define S3D_MODELS_OFF  0x140000u
#define S3D_MODELS_MAX  0x2C0000u

/* the memory for the page's models: 4-aligned, S3D_MODELS_MAX bytes. Every call counts as a new use of it (s2scene_generation() goes up): what a page loaded there
   earlier may be gone, so a page that keeps its models between visits asks whether the generation is still the one after its own load. */
uint8_t *s2scene_models(void);
uint32_t s2scene_generation(void);

/* ------------------------------------------------------------------------------------------------ the files */

/* A container of the ZIP (GFX/LOGO1.DAT, a .DAT of dat.h) open for its entries: its directory only, or all of it when the ZIP is deflated. One
   whole deflated container is kept at a time (the next s2scene_open of one replaces it), a stored one is read entry by entry. */
struct s2_cont {
    struct zip *zip;
    const struct zip_entry *e;
    const uint8_t *dir;                         /* what dat_find reads */
    uint32_t dir_len;
    uint32_t tail[(4096 + 16) / 4];             /* a stored container's last 4 KB: its directory */
};
int s2scene_open(struct zip *z, struct s2_cont *c, const char *path);
/* Entry `name` unpacked to out (max bytes; out 4-aligned): its size, or 0 if it is not there, does not fit or is damaged. The SDRAM is
   settled for the hardware afterwards (the geometry unit and the blitter read what the CPU wrote). */
uint32_t s2scene_entry(struct s2_cont *c, const char *name, uint8_t *out, uint32_t max);

/* ------------------------------------------------------------------------------------------------ the picture */

/* The lens: the race's (focal length 128, the eye 128 behind the camera: track.c), the centre of the view at column 159, row S3D_CY. */
#define S3D_CX      159
#define S3D_CY      100

/* The centre of the view in 1/16 pixel (the tests look for the original's), and the focal length times 16 */
void s2scene_lens(int cx16, int cy16, int focal16);

/* The 3D part of a frame into the back buffer `buffer`, the picture already drawn: waits until the blitter has done what is in the list
   (the picture), sets the unit up (the material file mlb, the clip, the projection) and forgets what the unit cached of earlier pages
   (fresh: the meshes or libraries changed since the last frame). Then pieces; at the end s2scene_end(). */
void s2scene_begin(const uint8_t *mlb, int buffer, int fresh);
/* The material file of the following pieces (waits until the unit is idle first: it reads the file while it works) */
void s2scene_mlb(const uint8_t *mlb);
/* Blocks (track.h) seen from cam, in the order given (far first) */
void s2scene_blocks(const struct track_cam *cam, const struct track_block *b, int n);
/* The pieces are with the unit; its work goes on while the CPU makes the page's text. */
void s2scene_end(void);

/* A mesh of one block (a badge, a letter tyre) as a block of the scene at (x, y, z), turned by a0 about x and a1 about the vertical, in 2048ths:
   the file's block 0 with the texture phase `phase` (frame n x phase >> 14 of its textures) */
void s2scene_object(const uint8_t *mesh, float x, float y, float z, int a0, int a1, uint32_t phase, struct track_block *out);

/* The camera of the original's pages: (x, y up, z), looking along -z, pitched by pitch (radians, + up) */
struct track_cam s2scene_camera(float x, float y, float z, float pitch);

/* The engine's easing (STARTL.EXE 0x3364d, 0x1016b, the table at 0x1240c0): a row keeps this fraction of the distance a frame, 0.5 + 0.005 row; 0x5f .. 0x62 are the slow
   glides of the options room's camera, 0x50 the badges' */
float s2scene_easing(int row);
/* An angle in radians eased towards target the short way round by the row's fraction (0x1016b), kept in 0 .. 2 pi */
void s2scene_ease_angle(float *v, float target, int row);

/* The distance of a point from a camera, squared, for ordering objects far to near */
float s2scene_dist2(const struct track_cam *cam, float x, float y, float z);

/* The angle a (2048ths) in radians */
static inline float s2scene_rad(int a) { return (float)a * (6.2831853f / 2048.0f); }
