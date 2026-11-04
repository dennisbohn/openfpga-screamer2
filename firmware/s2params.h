/*
 * Screamer 2's numbers for the car model, read from the game's .DTR text files (the format the game's
 * loader 0x31235 reads, OpenScreamer docs/NOTES2.md S2-5j): a section is the line with its name, then
 * words up to END_LIST; `d12` a whole number, `f1.5` a float, `x1f` hexadecimal, `*` begins a comment that
 * runs to the end of the line. The values fill the rows of the structures below in order.
 *
 *   DATA/GAME.DAT  CARS.DTR   CAR_PARAMS   4 teams x 5 cars (the fifth is the team's bonus car), 22 words a car
 *   DATA/MAPn.DAT  MAPnDAT.DTR TRACK_DATA  the far plane, the colours, the tyres' pressure and six ROAD KINDS
 *
 * No allocation, no floating point but float: a decimal is worked out as an integer over a power of ten, which
 * is the correctly rounded float whenever it has at most 7 significant digits (all of the game's have).
 * Tested against an independent reading of the files in Python (tools/s2/check_params.sh).
 */
#pragma once
#include <stdint.h>

#define S2_TEAMS        4
#define S2_TEAM_CARS    5
#define S2_CARS_TOTAL   (S2_TEAMS * S2_TEAM_CARS)
#define S2_KINDS        6               /* ROAD KINDS rows of a track */

/* CAR_PARAMS: the 22 words of a car in the file's order; this layout is what the game copies to car record
   +0x5c, word for word (s2car.c does the same). Wheels: 0 front +X, 1 rear +X, 2 front -X, 3 rear -X. */
struct s2_car_params {
    float max_speed;                    /* MAX_SPEED (km/h at the most revs in top gear) */
    float max_rpm;                      /* MAX_RPM */
    int32_t automatic;                  /* TIPO MARCE: 0 manual, 1 automatic */
    int32_t gears;                      /* NUMERO MARCE */
    float max_steer;                    /* MAX_STEER */
    float acceleration;                 /* ACCELERAZIONE */
    float grip;                         /* ADERENZA GLOBALE */
    float wheel_grip[4];                /* ADERENZA RUOTE F R (two lines: wheels 0 1, then 2 3) */
    float traction[4];                  /* TRAZIONE: the share of the drive each wheel gets */
    float drittizer;                    /* DRITTIZER */
    float brake;                        /* FRENO */
    int32_t susp_stiffness, susp_height, susp_extent;   /* SOSPENSIONI (rigidita', altezza, estensione) */
    int32_t cam_along, cam_up;          /* POSIZIONE TELECAMERA (the camera fixed to the car) */
};

/* a ROAD KIND row (0x24 bytes in the game's table at 0x11cdec + id x 0x24) */
struct s2_road_kind {
    int32_t id;                         /* the piece records' kind number; -1 = no row */
    float grip[3];                      /* the grip of the ground for slick, rain and snow tyres */
    int32_t rough;                      /* "sconn.": the roughness class 0 .. 4 (masks 0 15 31 63 127) */
    int32_t smoke[4];                   /* the dust's numbers (not used by the car model) */
};

struct s2_track_params {
    int32_t far_plane;                  /* profondita' di campo, negative */
    int32_t background[3], background_entry;
    int32_t asmoke[3], a2smoke[3];
    int32_t tyre_pressure[3];           /* the best tyre pressure for slick, rain, snow */
    struct s2_road_kind kind[S2_KINDS];
};

/* The text of a .DTR file: the section `name` read into the rows of n words (floats and whole numbers
   in the structures' order) up to its END_LIST: the number of words stored (at most max_words), 0 if the
   section is not there. */
int s2_dtr_words(const uint8_t *text, uint32_t size, const char *name, uint32_t *words, int max_words);

/* CAR_PARAMS of CARS.DTR text into out[S2_CARS_TOTAL] (team * 5 + car): 1 if all 20 cars were there */
int s2_params_cars(const uint8_t *text, uint32_t size, struct s2_car_params *out);
/* TRACK_DATA of MAPnDAT.DTR text: 1 if all of it was there */
int s2_params_track(const uint8_t *text, uint32_t size, struct s2_track_params *out);

/* The same from the game's containers (dat.h): DATA/GAME.DAT's CARS.DTR; DATA/MAPn.DAT's MAPnDAT.DTR.
   scratch (cap bytes, at least 16 KB) holds the unpacked text; 0 if a file or entry is missing. */
int s2_params_cars_dat(const uint8_t *game_dat, uint32_t size, uint8_t *scratch, uint32_t cap, struct s2_car_params *out);
int s2_params_track_dat(const uint8_t *map_dat, uint32_t size, int track, uint8_t *scratch, uint32_t cap,
                        struct s2_track_params *out);
