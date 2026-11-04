/*
 * The race's sound (s2sound.h)
 */
#include "s2sound.h"
#include "s2snd.h"
#include "s2car.h"
#include "race.h"
#include "music.h"
#include "hw.h"

#define FADE_FRAMES 150                         /* the music fades out over 3 s once the player has finished */

static int inited;
static int active;                              /* a race is on (s2snd_race_start .. s2snd_stop) */
static int cd[7] = { 9, 8, 6, 7, 4, 3, 5 };     /* CD_TRACKS of the game's MISC.DTR (read at the first race) */
static int cd_read;
static uint32_t last_vf, frames;
static int32_t fade;                            /* the music's volume, 0x8000 whole */
static int fading, faded;

void s2snd_init(struct zip *z)
{
    (void)z;
    if (inited) return;
    inited = 1;
    music_init(0, 0);                           /* the cue sheet (GAME.DAT); no MIDI songs in Screamer 2 */
}

void s2snd_race_start(int track)
{
    if (!inited || track < 1 || track > 7) return;
    active = 1;
    frames = 0;
    fading = faded = 0;
    fade = 0x8000;
    if (!cd_read) {
        uint32_t size;
        const uint8_t *game = s2car_game_dat(&size);
        if (game) s2snd_cd_tracks(game, size, cd);
        cd_read = 1;
    }
    int lo, hi, want = cd[track - 1];
    if (music_disc(&lo, &hi) != want) music_cd(want);   /* a restart of the race on the same track keeps its song */
    music_fade(0x8000);
}

void s2snd_frame(const struct track_cam *cam)
{
    (void)cam;
    if (!active) return;
    uint32_t vf = VID_FRAME(VID_STATUS), dvf = frames ? (vf - last_vf) & 0xFFFF : 1;
    last_vf = vf;
    if (dvf > 10) dvf = 10;
    struct race_hud h;
    race_get_hud(&h);
    if (h.finished && !fading && !faded) fading = 1;        /* the player has done his laps: the music goes */
    if (fading) {
        fade -= (int32_t)dvf * (0x8000 / FADE_FRAMES);
        if (fade <= 0) {
            fade = 0;
            fading = 0;
            faded = 1;
            music_fade(0);
            music_stop();
        } else {
            music_fade(fade);
        }
    }
    music_frame();
    frames++;
}

void s2snd_stop(void)
{
    if (!active) return;
    active = 0;
    music_stop();                               /* a read of the ring may be running: the track's load wants the slot */
}
