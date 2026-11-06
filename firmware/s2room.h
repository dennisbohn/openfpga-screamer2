/*
 * The OPTIONS ROOM of Screamer 2's menu (docs/screamer2/menus.md, row 8): the page the original shows after TEAM SELECTION and ENTER YOUR NAME
 * (STARTL.EXE 0x38e00 .. 0x39568; the earlier notes' 0x38d8d is the decompiler's name for it). The team's room, GFX/MENU<t>1.DAT, is built like a track
 * (track_build_s2() with no cars, track.h) and drawn by the track's engine, the camera flying between five stations of DATA/GEN's P_<t>.MCM (the
 * entry MENU<t>1.DAT holds the same file), one for each of the choices: ARCADE | CHAMPIONSHIP | TIME ATTACK | MULTIPLAYER | GAME SETTINGS.
 *
 *   LEFT / RIGHT   the next station (a fresh press, no repeat: the original's own); the camera glides there (about 1.3 s), the choice's name changes at once
 *   ENTER          (B or Start) the choice is taken: the picture fades out over 25 frames, the camera drifting towards the point it looks at
 *   ESC            (A or Select) back to TEAM SELECTION, at once
 *
 * The page's frame (every number read from the code; the camera's easing was checked against the running original frame by frame, docs/screamer2/menus.md):
 * the picture is the backdrop (MAP<t>.BCK, as the race draws it but with the eye line at row 100) and the room's placements seen from the camera, the band
 * y 170 .. 200 darkened (0x51868), "OPTIONS ROOM" centred at y 4 (font 0), the choice's name at y 175 (font 1), the arrows (MENUL.SPR 1 and 2, at (10, 172)
 * and (298, 172)) where there is a station to the left and right, while (clock & 0x7f) < 0x40; the clock is the page's hundredths, 0 where the page begins
 * and where a station is left. The textures of the room play by the engine's animation clock, 0.2 a turn a second (the race's is 0.3).
 */
#pragma once
#include <stdint.h>
#include "zip.h"
#include "s2menu.h"
#include "blit.h"
#include "track.h"

/* what the page ends with: the station's choice (the fade out is done), ESC, or S2ROOM_IDLE: 30 s without a button (s2attract.h; the fade out is done: the attract sequence follows) */
enum { S2ROOM_FAIL = 0, S2ROOM_ARCADE, S2ROOM_CHAMP, S2ROOM_ATTACK, S2ROOM_MULTI, S2ROOM_SETTINGS, S2ROOM_IDLE, S2ROOM_BACK = -1 };
#define S2ROOM_STATIONS 5
#define S2ROOM_RECORDS  3                       /* the records of P_<t>.MCM: the options room's stations, the CAR pages' five places, the CAR CUSTOMIZATION page's eight stations */

/* The SDRAM the room is built in (viewer.c's track window and the memory the track's containers are read to): the container GFX/MENU<t>1.DAT (0.8 to 0.9 MB),
   the image (3.0 to 3.6 MB), the build's scratch (72 KB) */
struct s2room_mem {
    uint8_t *container;
    uint32_t container_max;
    uint8_t *image;
    uint32_t image_max;
    uint8_t *scratch;
    uint32_t scratch_max;
};

/* The room of team 1 .. 4 built in m (its image replaces a track's: whoever keeps a track there builds it again) and P_<t>.MCM read; not again if the room in the window
   is this one (s2room_forget() says a track has taken the window since). 1, or 0 if a file is missing or the image does not fit (the log says which). The log has
   `room: T1 3,120,640 bytes in N ms`. */
int s2room_load(struct zip *z, const struct s2room_mem *m, int team);
/* the room in the window is not kept any more (the tests); a track built there since is noticed by the image's header */
void s2room_forget(void);
/* The menu's room: s2menu_room (s2menu.h) when the options room is the menu. The memory is the viewer's track window (trackmem.h), whose record of what its image holds it clears. */
int s2room_menu(const struct s2_menu_cfg *c);

/* The page until a choice is taken or ESC: S2ROOM_*; S2ROOM_FAIL if the room could not be built or the toolkit (s2gfx_init()) is not up (nothing is shown).
   The station the player was at stays for the next visit, as the original keeps it (0x132378: zero when the menu starts). */
int s2room_run(struct zip *z, const struct s2room_mem *m, int team);

/* ------------------------------------------------------------------------------------------------ the camera (for the tests)
   The record of the original's camera (0x131ea4): the place (engine x, y up, z) and the angles a0 (pitch: 0 looking along the horizon, 1024 .. 2047 down by
   2048 - a0) and a1 (heading) in 2048ths; pitch and head are the page's own state, the angles in radians (0 .. 2 pi) that the record's are cut from. */
struct s2room_cam {
    int32_t pos[3];
    float pitch, head;
    int a0, a1;
    int row;                                    /* the easing row of the place: 0x5f at first, 0x55 after the first move */
};
/* P_<t>.MCM as DATA/GEN.DAT has it (444 bytes; the room's container has the same file with the third record empty: 252 bytes): three records of [n], n places and n aims of three
   32-bit whole numbers each: record 0 the options room's stations (n = 5), record 1 the CAR pages' cars (n = 5: the places are 0, the aims are the cars' places, the fifth is the
   bonus car's), record 2 the CAR CUSTOMIZATION page (n = 8: aim 0 is the car's place, place k and aim k the camera of station k, place 0 is 0). Into the tables; 1, or 0 if the file is
   too short or its first record has fewer than five stations */
int s2room_stations(const uint8_t *file, uint32_t size);
/* a station's camera place (aim 0) or the point it looks at (aim 1), the engine's x, y up, z: record r (0 .. 2), station k; 0 if the record has none */
const int32_t *s2room_record(int r, int k, int aim);
int s2room_record_count(int r);
/* the options room's stations (record 0) */
const int32_t *s2room_station(int k, int aim);
/* the page's camera and station as of its last frame (what the tests look at) */
const struct s2room_cam *s2room_camera(int *station);
/* the start of a visit at station k: the place five times as far along the line from the aim point, the angles looking at it */
void s2room_cam_start(struct s2room_cam *c, int station);
/* one frame at the station: the place eased towards it (towards the aim point once the picture is fading out: fade is its level, 0.04 a frame from the frame after ENTER, 0
   before), the angles towards the look-at angles; the original's easing rows (0x3364d, 0x1016b) */
void s2room_cam_step(struct s2room_cam *c, int station, float fade);

/* ------------------------------------------------------------------------------------------------ for the CAR and CAR CUSTOMIZATION pages (s2garage.h)
   The team's room is one for the whole menu: the options room, the CAR pages of time attack and the championship and the CAR CUSTOMIZATION page all draw the same image of
   the track window (the original loads it once and its load lists keep it: 0x1132b0, 0x1132e0), built where the first of them is shown, and again after a race has taken the window. */

/* The room of the team in the track window (built if it is not there: the screen goes black for the 1.5 s of the build) and the viewer told that its image is the window's. 1, 0 if it could not be built */
int s2room_open(int team);
/* The eased place (0x3364d): pos moves to target by the row's share of the distance (the easing table's: 0.5 + 0.005 row kept each frame), the sum cut towards zero */
void s2room_ease_place(int32_t pos[3], const int32_t target[3], int row);
/* The room's ASHADOW.3DX (one flat quad), read with the room, or 0 */
const uint8_t *s2room_shadow(void);
/* The table of darkening of the room's palette (s2gfx_dark_table), made on first use after the room was built, for the band and the boxes the pages darken */
const uint8_t *s2room_dark(void);
/* 1 once after the room was (re)built: the geometry unit must forget what it cached (meshes and materials by their place) */
int s2room_fresh(void);

/* One picture of the room's engine (the options room's, the CAR pages', the CAR CUSTOMIZATION page's): the backdrop (as the options room has it, the eye line at row 100) and the
   ground colour, the room's pieces seen from cam with the objects (track.h: cars, their shadows) sorted in among them as the engine does, and a box of the picture (bx, by, bw, bh) darkened
   through the table dark (0: none) once the units are done. piece is what takes each piece (track_hw_piece; 0: hw_piece: a page whose objects have material files of their
   own switches the unit's). fresh: the unit forgets its caches first. The list l holds the picture's commands before and is empty (aimed at the same buffer) after, for the page's sprites and text. */
struct s2room_view {
    const struct track_cam *cam;
    struct track_object *objs;
    int nobj;
    void (*piece)(const struct gu_piece *p);
    int fresh;
    const uint8_t *dark;
    int bx, by, bw, bh;
};
void s2room_scene(struct blit_list *l, const struct s2room_view *v);
