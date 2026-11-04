/*
 * Screamer 2's sound: the samples of DATA/SOUND.DAT and the game's rules for them, without any
 * hardware (the driver is s2sound.c; tools/s2/check_sound.sh tests this file natively).
 *
 * SOUND.DAT is a .DAT container (dat.h) of .RAW samples, 8-bit unsigned mono, stored, and of
 * .DTR tables. GAME.DTR lists the 33 samples of the race by number, in the section SOUND_DATA, per
 * sample two lines:
 *     sNAME.RAW  xID  xLOOPLEN  xLOOPCOUNT  xVOLUME        (hex; x-1 for the loop count = round and round)
 *     dRATE  dBITS  dCHANNELS  dFORMAT                      (decimal: Hz, 8, 0 = mono, 0 = unsigned)
 * The race plays them through the game's sound manager (OpenScreamer docs/NOTES2.md, S2-5zf); the
 * rules below are read from the game's program (STARTL.EXE, addresses in the comments).
 *
 *   0 .. 7      M_PORSCH M_CELICA M_ACUV1 M_MK5 M_STOCK M_CORV M_MONTER M_PICK: the engines by model (a
 *               fifth model is its team's own: 4 + team)
 *   8 .. 0xb    SKID1 .. SKID4: the tyres' squeal, by the road's kind
 *   0xc .. 0xe  HIT1 .. HIT3: the car against a wall, by how hard
 *   0xf .. 0x11 CRASH1 .. CRASH3: a car against a car, by how hard
 *   0x12        GROUND1: a landing; 0x13 .. 0x18 TIRES1 .. TIRES6: rolling on the road's kinds
 *   0x19 .. 0x20  T_*: the engines again, for a tunnel (the sample + 0x19)
 */
#pragma once
#include <stdint.h>

struct s2_road_kind;

#define S2SND_SAMPLES   0x21
#define S2SND_HIT       0xC
#define S2SND_CRASH     0xF
#define S2SND_GROUND    0x12
#define S2SND_TUNNEL    0x19            /* added to an engine's sample number in a tunnel */
#define S2SND_RANGE     50000.0f        /* how far an engine carries (0x1ff8e) */
#define S2SND_FX_MASTER 0x2000          /* the effects' master volume, of 0x7fff: Screamer 1's two thirds of the game's 0x6000 (sound.h)
                                           halved again, since four engines, a squeal and a knock clipped the sum in the simulation (the
                                           music is at 0x4000, music.c). How the game balances them is not in its program (the CD's
                                           analogue mix): to be set by ear */

struct s2snd_sample {
    uint32_t addr;                      /* the first byte, as the mixer takes it: the SDRAM byte address */
    uint32_t len;                       /* bytes, 0: the sample is not there */
    uint32_t rate;                      /* Hz */
    uint32_t vol;                       /* the sample's own volume, 0 .. 0x7fff */
    uint8_t loop;                       /* 1: round and round, 0: once */
};

/* GAME.DTR of SOUND.DAT (dat, size bytes; base: the SDRAM byte address of dat[0]) into out[S2SND_SAMPLES]:
   the number of samples that are there (a missing name, a sample that is not stored or not 8-bit mono
   leaves its row with len 0) */
int s2snd_parse(const uint8_t *dat, uint32_t size, uint32_t base, struct s2snd_sample *out);

/* CD_TRACKS of DATA/GAME.DAT's MISC.DTR: the CD track of each of the 7 tracks, in the game's order (0x2eb98
   plays CD_TRACKS[track - 1]); 1 if it read all seven */
int s2snd_cd_tracks(const uint8_t *game_dat, uint32_t size, int out[7]);

/* The mixer's step (2.16 samples per output value of 48 kHz) of a sample played at rate Hz, in 1 .. 0x3ffff */
uint32_t s2snd_step(uint32_t rate);

/* The registers' volume: the game's volume of a voice (0 .. 0x7fff, the sample's own times the distance
   and so on), the effects' master volume (sound.c's two thirds of the game's) and the pan (0 left, 0x8000
   the middle, 0xffff right; the middle is full on both sides, a voice off it fades out of the far one in a
   straight line, as HMI's driver does) as AU_VOL: left in bits 7..0, right in 15..8 */
uint32_t s2snd_vol_reg(uint32_t vol, int32_t pan);

/* An engine's sample number (0x1feaf): the model, or for the fifth model of a team (4) 4 + team */
int s2snd_engine_sample(int team, int model);
/* The road kinds with a tunnel's sound (0x1fe67: 7, 13, 34, 43) */
int s2snd_tunnel(int kind);

/* A sound heard from a distance (0xaf338, the game's sound manager, the 0xaf168 of OpenScreamer's notes; an engine's call is at 0x1ffbd): the source at
   distance d from the listener, last_d the distance a frame before (< 0: none yet), across how far the source
   is along the listener's side (positive: left), pitch the engine's 2 x revs / most. Within the range:
   rate = own x pitch^2, plus that x (last_d - d) / range x 5 x doppler (the game's 5 per frame of its own;
   doppler scales it to ours); volume = the sample's x ((range - d) / range)^2; pan = 32768 -
   32767 x cos((1 - across / range) x 1.87). Beyond it: the sample's 11025 Hz, silent, in the middle. */
struct s2snd_heard { uint32_t rate, vol; int32_t pan; };
void s2snd_heard(const struct s2snd_sample *s, float pitch, float d, float last_d, float across, float range,
                 float doppler, struct s2snd_heard *out);

/* The tyres (0x1b36f) of the car followed: the road kind's two sound numbers (roll: its smoke[2], skid:
   smoke[3]), the tyres' grip (the car's +0xc34) and wheels on the ground (+0x510), the speed (+0x500). The
   sample to play and its volume (0 .. 0x7fff, no distance: the player's own); -1 for none. A squeal
   (grip under 0.93: (0.93 - grip) / 0.1 of 29952) wins where the kind has one, else the rolling sound
   (the speed, 50 at most, x 655), where the kind has one */
int s2snd_tyres(int roll, int skid, float grip, float on_ground, float speed, uint32_t *vol);

/* A wall knock (0x29571) of strength hard: the sample (HIT1 .. HIT3 over 500, 4000, 10000), 0 for none */
int s2snd_wall_sample(float hard);
/* A car against a car (0x245a3) of strength knock: CRASH1 .. CRASH3 (over 5000, 10000) */
int s2snd_car_sample(float knock);
