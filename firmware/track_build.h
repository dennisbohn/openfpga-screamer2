/*
 * The track image (track.h) built on the Pocket from the game's original containers, byte for
 * byte as tools/make_track.py builds it: the entries unpacked with dat.c, the sections made by
 * tb_mmp.c, tb_spl.c, tb_gfx.c and tb_hud.c, in make_track.py's order on 256-byte boundaries
 * from 0x100, the gaps zero; then the header (version 6) and the CRC-32 of each 256 KB block.
 */
#pragma once
#include <stdint.h>

/* The five containers as the data slots load them, each with its size: GFX/MAPnH.DAT,
   GFX/GAME.DAT, DATA/MAPn.DAT, DATA/GEN.DAT, DATA/LOCAL.DAT */
struct track_files {
    const uint8_t *gfx, *game, *data, *gen, *local;
    uint32_t gfx_size, game_size, data_size, gen_size, local_size;
};

/* The game's difficulty, 0 ROOKIE, 1 NORMAL (the default), 2 PRO: the opponents and TRACK_DATA
   come from MAPnA.DTR, MAPnB.DTR or MAPnC.DTR (0x13251) */
extern int track_build_difficulty;

/* What a build did: the entry or section it stopped at ("" when it got through), and the
   CPU cycles of the whole build and of the unpacking in it (0 on the PC) */
struct track_build_info {
    char fail[20];
    uint32_t cycles, unpack_cycles;
    uint32_t scratch;                   /* the scratch the build used, bytes */
    int32_t misc_delta;                 /* Screamer 2: the offset to add to a material offset of the track's MISC.MLB (the small objects' materials: an animation's
                                           mesh, GFX/MAP<n>H01.DAT's <name>.3DX) to make it one the image's geometry unit takes (the records of MISC.MLB lie after the
                                           cars' in front of the MLB: negative); 0 for an image without it (a room) */
};

/* Track n (1..9, MAPn) into out (cap bytes, any alignment): returns the image's size, 0 if a
   container or entry is missing or malformed, or the image does not fit (at most 7 MB: 28
   blocks). The small entries are unpacked into scratch (scratch_cap bytes, 270 KB for track
   1); MAPn.MLB, MAPn.UNR and the cars' .3DX files straight to their place in out. out and
   scratch are read back while they are written: ordinary memory, the SDRAM through the cache.
   Track 1 (4,166,400 bytes) took 237 M cycles in the simulation, 3.2 s: 73 M to unpack, the
   rest mostly the camera path (soft-float doubles) and the CRC-32 over 4 MB. */
uint32_t track_build(const struct track_files *f, int n, uint8_t *out, uint32_t cap,
                     uint8_t *scratch, uint32_t scratch_cap, struct track_build_info *info);

/*
 * Screamer 2 (docs/screamer2/comparison.md): the same from the three containers a track needs, as the ZIP
 * reader (zip.h) hands them over: GFX/MAPnH01.DAT, DATA/MAPn.DAT and GFX/ENVPn01.DAT (the cars' windows'
 * reflection), and the cars of the grid, each already unpacked (s2teams.h).
 * Version 7 of the image (track.h), byte for byte tools/s2/make_track_s2.py's: the track's own sections and
 * the cars of the grid, each its own team's (a race draws four: the player's and three computers'), with
 * their material libraries merged after the track's (the car materials in a section in front of the MLB), then
 * the reflection picture (the first material of ENV.MLB, its frames tiled 2 x 2 into 256 x 256, 64 KB each)
 * with the windows' materials pointing to it; no camera path, markers, race screen or opponents; the player
 * starts at the first road point; the header holds the far plane (TRACK_DATA). Car model k of the image is
 * car[k]: the race draws grid slot k with it. The cars section has a row for each (a stand-in, CARS.DTR's
 * numbers are the physics' own), whose livery word names the car: team * 256 + car, as the files number them
 * (s2car.c finds a car's wheels by it). The scratch needs 0.6 MB for track 4, the largest; the image 3.5 to 5.4 MB
 * (14 to 21 blocks of 256 KB; 28 at most). env may be 0: the windows then keep the cars' own (placeholder) picture.
 *
 * A race's MISC.MLB is one more library, and after the libraries and the reflection the MLB section holds the see-through step's work (smg.h): the textures of routine 208 and 224
 * copied with the hardware's rows, the shadow's and the two smokes' meshes, the table of 64 rows and its directory (the section's last 256 bytes, track_fx()).
 *
 * With ncars 0 (data and env 0) the image is a menu's room (docs/screamer2/menus.md, row 8): f->gfx is GFX/MENU<t>1.DAT, whose entries MAP<t>.MLB, .UNR, .MMP,
 * .COL and .BCK are a track's (n is the team), the scenery only: no car sections (so no car materials in front of the MLB), no road points or walls if the map has
 * none (the start stays 0), the menus' far plane 43,808 and the colour 0 where a track has TRACK_DATA. tools/s2/make_track_s2.py (S2ROOM=<t>) is the reference.
 */
#define S2_CARS     5                   /* models an image may hold: a race has four, the physics checks use a team's five */
struct s2_car {                         /* AUTO<car>.MLB and .3DX of GFX/CARS<team>01.DAT, unpacked */
    const uint8_t *mlb, *x3d;
    uint32_t mlb_size, x3d_size;
    uint8_t team, car;                  /* 1..4, 1..5 as the files number them */
};
struct s2_files {
    const uint8_t *gfx, *data, *env;
    uint32_t gfx_size, data_size, env_size;
    const struct s2_car *car;           /* ncars of them: model k of the image */
    int ncars;                          /* 1 .. S2_CARS */
};
uint32_t track_build_s2(const struct s2_files *f, int n, uint8_t *out, uint32_t cap,
                        uint8_t *scratch, uint32_t scratch_cap, struct track_build_info *info);

/* A .3DX mesh (size bytes, in place) with every face's material offset (face +4, an offset in the mesh's own material file) moved by delta: onto the records a library was
   given in front of the image's MLB (negative). Every block of the header's table at the three levels of detail, every face of its 16 rows; moved only while it is not yet
   negative, so a face in several rows is moved once. 1, or 0 if the file is malformed or an offset would leave the 16 bits. The cars' meshes (track_build_s2) and the scenery's
   (s2anim.c: delta is info->misc_delta) are moved so. */
int s2_faces_onto(uint8_t *x, uint32_t size, int32_t delta);

/* A mesh of the track's graphics container (gfx: GFX/MAP<n>H01.DAT, as the viewer read it whole), <name>.3DX (the name in either case), unpacked to dst (cap bytes) with its faces
   moved onto the image's copy of MISC.MLB's records by misc_delta (track_build_info.misc_delta; not 0): its size, 0 if it is not there, does not fit or cannot be moved. The small
   objects of a race are loaded so: the scenery's (s2anim.c) and the obstacles' and sparks' (s2props.c). */
uint32_t s2_mesh_load(const uint8_t *gfx, uint32_t gfx_size, const char *name, int32_t misc_delta, uint8_t *dst, uint32_t cap);
