/*
 * The race's sound (s2sound.h)
 */
#include "s2sound.h"
#include "s2snd.h"
#include "s2car.h"
#include "s2params.h"
#include "race.h"
#include "track.h"
#include "music.h"
#include "zip.h"
#include "hw.h"
#include "dlog.h"
#include <math.h>
#include <string.h>

#define SOUND_MEM   (SDRAM_BASE + 0x1C00000)    /* SOUND.DAT, up to 2 MB; the ring of the music follows (music.c), docs/soc.md */
#define SOUND_MAX   0x200000u
#define V_ENGINE    0                           /* voices 0 .. 3: the cars' engines */
#define V_TYRE      5                           /* the player's tyres */
#define V_KNOCK     6                           /* voices 6 .. 9: knocks */
#define KNOCKS      4
#define ALL_VOICES  0x3FFu
#define DOPPLER     (50.0f / 30.0f)             /* the game's shift is per frame of its own (30 a second); ours are 50 */
#define KNOCK_GAP   3                           /* frames between two knocks, unless the second is harder (the game's frames are 1/17 to 1/30 s apart) */
#define FADE_FRAMES 150                         /* the music fades out over 3 s once the player has finished */
#define MIN_RATE    200                         /* Hz: an engine at rest still turns the sample */

union word { int32_t i; uint32_t u; float f; };
#define RF(c, o)    (((const union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o)    (((const union word *)(c)->rec)[(o) >> 2].i)

static struct s2snd_sample smp[S2SND_SAMPLES];
static int inited, ready;                       /* init done; SOUND.DAT there with samples */
static int active;                              /* a race is on (s2snd_race_start .. s2snd_stop) */
static int cd[7] = { 9, 8, 6, 7, 4, 3, 5 };     /* CD_TRACKS of the game's MISC.DTR (read at the first race) */
static int cd_read;

/* per engine: the sample playing (-1 none), the distance a frame before (< 0 none), what the voice has */
static struct { int id; float last; uint32_t step, reg; } eng[RACE_CARS];
static struct { int id; uint32_t reg; } tyre;
static int ground_before;                       /* the player's wheels on the ground a frame before */
static uint32_t knock_age, knock_rank, knock_next;
static uint32_t last_vf, frames;
static int32_t fade;                            /* the music's volume, 0x8000 whole */
static int fading, faded, cost_logged;
static uint64_t sum_fx, sum_music;
static uint32_t max_fx, max_music;

static void log_cost(void);

static void play(int v, int id, uint32_t step, uint32_t reg)
{
    const struct s2snd_sample *s = &smp[id];
    AU_ADDR(v) = s->addr;
    AU_END(v) = s->addr + s->len;
    AU_LOOP(v) = s->loop ? 255u << 24 | s->len : 0;
    AU_STEP(v) = step;
    AU_VOL(v) = reg;
    AU_PLAY = 1u << v;
}

void s2snd_init(struct zip *z)
{
    if (inited) return;
    inited = 1;
    uint32_t t0 = rdcycle();
    const struct zip_entry *e = z ? zip_find(z, "DATA/SOUND.DAT") : 0;
    if (!e) {
        dlog_str("sound: DATA/SOUND.DAT is not in the ZIP, no effects\n");
    } else if (e->size > SOUND_MAX || zip_read(z, e, (uint8_t *)(uintptr_t)SOUND_MEM, SOUND_MAX)) {
        dlog_str("sound: SOUND.DAT could not be read, no effects\n");
    } else {
        dcache_invalidate();
        int n = s2snd_parse((const uint8_t *)(uintptr_t)SOUND_MEM, e->size, SOUND_MEM - SDRAM_BASE, smp);
        ready = n > 0;
        dlog_str("sound: SOUND.DAT ");
        dlog_dec((int32_t)(e->size / 1024));
        dlog_str(" KB, ");
        dlog_dec(n);
        dlog_str(" samples, ");
        dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
        dlog_str(" ms\n");
    }
    music_init(0, 0);                           /* the cue sheet (GAME.DAT); no MIDI songs in Screamer 2 */
}

void s2snd_race_start(int track)
{
    if (!inited || track < 1 || track > 7) return;
    AU_STOP = ALL_VOICES;
    active = 1;
    for (int i = 0; i < RACE_CARS; i++) {
        eng[i].id = -1;
        eng[i].last = -1.0f;
    }
    tyre.id = -1;
    ground_before = 4;
    knock_age = 100;
    knock_rank = knock_next = 0;
    frames = 0;
    fading = faded = 0;                         /* cost_logged stays: the cost line once per power-up (the log is 4 KB) */
    fade = 0x8000;
    sum_fx = sum_music = 0;
    max_fx = max_music = 0;
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

/* car i's engine (0x1f756): the sample by its model and the road under it, heard from the camera; the player's own is
   the nearest */
static void engine(int i, const struct track_cam *cam, float cs, float sn)
{
    const struct s2car_state *c = race_car(i);
    int id = s2snd_engine_sample(i, i) + (s2snd_tunnel(RI(c, 0x6b0)) ? S2SND_TUNNEL : 0);
    if (id >= S2SND_SAMPLES || !smp[id].len) return;
    float dx = c->pos[0] - cam->x, dy = c->pos[1] - cam->y;
    float d = sqrtf(dx * dx + dy * dy), most = RF(c, 0x60);
    if (eng[i].last < 0.0f) eng[i].last = d;
    struct s2snd_heard h;
    s2snd_heard(&smp[id], most > 0.0f ? c->rpm / most * 2.0f : 0.0f, d, eng[i].last, dx * cs + dy * sn, S2SND_RANGE, DOPPLER, &h);
    eng[i].last = d;
    uint32_t step = s2snd_step(h.rate < MIN_RATE ? MIN_RATE : h.rate), reg = s2snd_vol_reg(h.vol, h.pan);
    if (eng[i].id != id) {                      /* the first time, or the tunnel's version of the sample */
        eng[i].id = id;
        play(V_ENGINE + i, id, step, reg);
    } else {
        if (step != eng[i].step) AU_STEP(V_ENGINE + i) = step;
        if (reg != eng[i].reg) AU_VOL(V_ENGINE + i) = reg;
    }
    eng[i].step = step;
    eng[i].reg = reg;
}

/* the player's tyres (0x1b36f): a squeal, or the rolling of a rough road; at the sample's own rate */
static void tyres(const struct s2car_state *c)
{
    const struct s2_road_kind *k = s2car_road_kind(RI(c, 0x6b0));
    uint32_t vol = 0;
    int id = s2snd_tyres(k->smoke[2], k->smoke[3], RF(c, 0xC34), RF(c, 0x510), RF(c, 0x500), &vol);
    if (id < 0 || id >= S2SND_SAMPLES || !smp[id].len) {
        if (tyre.id >= 0) AU_STOP = 1u << V_TYRE;
        tyre.id = -1;
        return;
    }
    uint32_t reg = s2snd_vol_reg(vol, 0x8000);
    if (tyre.id != id || !(AU_PLAY & (1u << V_TYRE))) {
        tyre.id = id;
        play(V_TYRE, id, s2snd_step(smp[id].rate), reg);
    } else if (reg != tyre.reg) {
        AU_VOL(V_TYRE) = reg;
    }
    tyre.reg = reg;
}

/* a knock of the player's car on the next of the knocks' voices, at the sample's own volume and rate. The game starts
   one sound for each of its frames of contact and lets the earlier ones play on; here one every KNOCK_GAP frames,
   or at once if it is harder than the last. */
static void knock(int id, uint32_t rank)
{
    if (id >= S2SND_SAMPLES || !smp[id].len) return;
    if (knock_age < KNOCK_GAP && rank <= knock_rank) return;
    knock_age = 0;
    knock_rank = rank;
    play(V_KNOCK + (int)(knock_next++ % KNOCKS), id, s2snd_step(smp[id].rate), s2snd_vol_reg(smp[id].vol, 0x8000));
}

void s2snd_frame(const struct track_cam *cam)
{
    if (!active) return;
    uint32_t t0 = rdcycle();
    uint32_t vf = VID_FRAME(VID_STATUS), dvf = frames ? (vf - last_vf) & 0xFFFF : 1;
    last_vf = vf;
    if (dvf > 10) dvf = 10;
    struct s2car_state *p = race_car(0);
    if (ready) {
        float cs = cosf(cam->yaw), sn = sinf(cam->yaw);     /* the listener's left, in x and y (the camera's heading - pi: 0xaf338) */
        for (int i = 0; i < race_cars() && i < RACE_CARS; i++) engine(i, cam, cs, sn);
        tyres(p);
        knock_age += dvf;
        float hard = p->hit_hard, hit = race_knock_take();
        p->hit_hard = 0.0f;
        if (hit > 0.0f) {
            int id = s2snd_car_sample(hit);
            knock(id, 4u + (uint32_t)(id - S2SND_CRASH));
        } else if (hard > 0.0f && s2snd_wall_sample(hard)) {
            int id = s2snd_wall_sample(hard);
            knock(id, 1u + (uint32_t)(id - S2SND_HIT));
        } else if (ground_before == 0 && p->on_ground > 0) {    /* from the air onto the ground (0x1bef1) */
            knock(S2SND_GROUND, 0);
        }
        ground_before = p->on_ground;
    }
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
    uint32_t t1 = rdcycle();
    music_frame();
    uint32_t t2 = rdcycle();
    frames++;
    sum_fx += t1 - t0;
    sum_music += t2 - t1;
    if (t1 - t0 > max_fx) max_fx = t1 - t0;
    if (t2 - t1 > max_music) max_music = t2 - t1;
    if (frames == 250 && !cost_logged) log_cost();          /* 5 s into the race: in the log even if the core is quit in the race */
}

/* what a frame of the sound cost: mean / worst cycles of the effects and of the music's ring (music_frame) */
static void log_cost(void)
{
    cost_logged = 1;
    dlog_str("sound: ");
    dlog_dec((int32_t)frames);
    dlog_str(" fr, effects ");
    dlog_dec((int32_t)(sum_fx / frames));
    dlog_char('/');
    dlog_dec((int32_t)max_fx);
    dlog_str(", music ");
    dlog_dec((int32_t)(sum_music / frames));
    dlog_char('/');
    dlog_dec((int32_t)max_music);
    dlog_str(" cycles mean/max\n");
}

void s2snd_stop(void)
{
    if (!active) return;
    active = 0;
    AU_STOP = ALL_VOICES;
    music_stop();                               /* a read of the ring may be running: the track's load wants the slot */
    if (frames >= 50 && !cost_logged) log_cost();
}
