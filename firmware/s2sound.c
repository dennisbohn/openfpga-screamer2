/*
 * The race's sound (s2sound.h)
 */
#include "s2sound.h"
#include "s2snd.h"
#include "s2anim.h"
#include "s2props.h"
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
#define V_TYRE2     4                           /* combat mode: the second player's tyres */
#define V_TYRE      5                           /* the player's tyres */
#define V_KNOCK     6                           /* voices 6 .. 9: knocks */
#define KNOCKS      4
#define V_MENU      10                          /* the menu's clicks (s2sound_menu) */
#define V_ANIM      11                          /* voices 11 .. 31: the sounds of the track's animated scenery, one for each row (s2anim.h) */
#define ANIMS       (AU_VOICES - V_ANIM)
#define ALL_VOICES  0xFFFFFFFFu
#define DOPPLER     (50.0f / 30.0f)             /* the game's shift is per frame of its own (30 a second); ours are 50 */
#define KNOCK_GAP   3                           /* frames between two knocks, unless the second is harder (the game's frames are 1/17 to 1/30 s apart) */
#define MIN_RATE    200                         /* Hz: an engine at rest still turns the sample */

union word { int32_t i; uint32_t u; float f; };
#define RF(c, o)    (((const union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o)    (((const union word *)(c)->rec)[(o) >> 2].i)

static struct s2snd_sample smp[S2SND_SAMPLES], menu_smp[S2SND_MENU_SAMPLES], amb[S2SND_AMBIENT];
static uint32_t sound_size;                     /* SOUND.DAT's size */
static int inited, ready;                       /* init done; SOUND.DAT there with samples */
static int active;                              /* a race is on (s2snd_race_start .. s2snd_stop) */
static int cd[7] = { 9, 8, 6, 7, 4, 3, 5 };     /* CD_TRACKS of the game's MISC.DTR (read at the first race) */
static int cd_read;

/* per engine: the sample playing (-1 none), the distance a frame before (< 0 none), what the voice has */
static struct { int id; float last; uint32_t step, reg; } eng[RACE_CARS];
static struct { int id; uint32_t reg; } tyre[2];                /* the first player's tyres, and the second's in combat mode */
static const int tyre_voice[2] = { V_TYRE, V_TYRE2 };
static struct { int id; float last; uint32_t step, reg; } scene[ANIMS];   /* the voice of each animated row: its ambient sample (-1 none), the distance a frame before (< 0 none) and what it has */
static int ground_before;                       /* the player's wheels on the ground a frame before */
static uint32_t knock_age, knock_rank, knock_next;
static uint32_t last_vf, frames;
static uint32_t fx_level = 0x8000;              /* the race's effects at this of 0x8000 (s2sound_effects: the pause's and the end screen's ease) */
static int cost_logged;
static uint64_t sum_fx, sum_music;
static uint32_t max_fx, max_music;

static void log_cost(void);

/* a voice's register (left and right volume, a byte each) at the effects' level */
static uint32_t at_level(uint32_t reg)
{
    if (fx_level == 0x8000) return reg;
    return (reg & 255) * fx_level >> 15 | ((reg >> 8 & 255) * fx_level >> 15) << 8;
}

static void play_sample(int v, const struct s2snd_sample *s, uint32_t step, uint32_t reg)
{
    AU_ADDR(v) = s->addr;
    AU_END(v) = s->addr + s->len;
    AU_LOOP(v) = s->loop ? 255u << 24 | s->len : 0;
    AU_STEP(v) = step;
    AU_VOL(v) = reg;
    AU_PLAY = 1u << v;
}

/* a voice of the race: its register is the effects' level times the sample's */
static void play(int v, int id, uint32_t step, uint32_t reg)
{
    play_sample(v, &smp[id], step, at_level(reg));
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
        sound_size = e->size;
        int n = s2snd_parse((const uint8_t *)(uintptr_t)SOUND_MEM, e->size, SOUND_MEM - SDRAM_BASE, smp);
        ready = n > 0;
        int m = s2snd_parse_menu((const uint8_t *)(uintptr_t)SOUND_MEM, e->size, SOUND_MEM - SDRAM_BASE, menu_smp);
        dlog_str("sound: SOUND.DAT ");
        dlog_dec((int32_t)(e->size / 1024));
        dlog_str(" KB, ");
        dlog_dec(n);
        dlog_str(" + ");
        dlog_dec(m);
        dlog_str(" menu samples, ");
        dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
        dlog_str(" ms\n");
    }
    music_init(0, 0);                           /* the cue sheet (GAME.DAT); no MIDI songs in Screamer 2 */
}

int s2snd_music_later;

void s2snd_race_start(int track)
{
    if (!inited || track < 1 || track > 7) return;
    AU_STOP = ALL_VOICES;
    active = 1;
    for (int i = 0; i < RACE_CARS; i++) {
        eng[i].id = -1;
        eng[i].last = -1.0f;
    }
    tyre[0].id = tyre[1].id = -1;
    for (int i = 0; i < ANIMS; i++) {
        scene[i].id = -1;
        scene[i].last = -1.0f;                  /* (0xaf45c: the distances forgotten) */
    }
    memset(amb, 0, sizeof amb);
    if (ready) s2snd_parse_ambient((const uint8_t *)(uintptr_t)SOUND_MEM, sound_size, SOUND_MEM - SDRAM_BASE, track, amb);
    ground_before = 4;
    knock_age = 100;
    knock_rank = knock_next = 0;
    frames = 0;                                 /* (cost_logged stays: the cost line once per power-up, the log is 4 KB) */
    fx_level = 0x8000;
    sum_fx = sum_music = 0;
    max_fx = max_music = 0;
    if (!s2snd_music_later) s2snd_music(track);
    else music_stop();                          /* (the attract race: the song of the menu, if any, is not the track's; it comes with the picture) */
}

void s2snd_music(int track)
{
    if (!inited || track < 1 || track > 7) return;
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
        if (reg != eng[i].reg) AU_VOL(V_ENGINE + i) = at_level(reg);
    }
    eng[i].step = step;
    eng[i].reg = reg;
}

/* a player's tyres (0x1b36f): a squeal, or the rolling of a rough road; at the sample's own rate. The game has them for the car the camera follows on its voice 5, and in
   combat mode (the mode word's 0x40, 0x1b77e) for every car on voice 5 + the car: here the second player's are voice 4, the knocks have 6 and up. */
static void tyres(int p, const struct s2car_state *c)
{
    const int v = tyre_voice[p];
    const struct s2_road_kind *k = s2car_road_kind(RI(c, 0x6b0));
    uint32_t vol = 0;
    int id = s2snd_tyres(k->smoke[2], k->smoke[3], RF(c, 0xC34), RF(c, 0x510), RF(c, 0x500), &vol);
    if (id < 0 || id >= S2SND_SAMPLES || !smp[id].len) {
        if (tyre[p].id >= 0) AU_STOP = 1u << v;
        tyre[p].id = -1;
        return;
    }
    uint32_t reg = s2snd_vol_reg(vol, 0x8000);
    if (tyre[p].id != id || !(AU_PLAY & (1u << v))) {
        tyre[p].id = id;
        play(v, id, s2snd_step(smp[id].rate), reg);
    } else if (reg != tyre[p].reg) {
        AU_VOL(v) = at_level(reg);
    }
    tyre[p].reg = reg;
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

/* the sounds of the animated scenery (0x2cee4, 0x2d543: the game's sound manager's 0xaf338): each row that is heard has its voice, the sample looping, heard from the camera
   within 80,000 units, the volume the sample's x ((80,000 - distance) / 80,000)^2, panned by the side, the rate the sample's own with the Doppler shift as an engine's (measured
   in the running original, tools/s2/dosbox_anim.py --voices: 10,857 to 11,025 Hz for an 11,025 Hz sample; the game's call has a pitch of 0, its rate comes out as the sample's
   all the same). The voice starts on the first frame the row is heard and goes when it is not (the distance a frame before stays, as the game's does). */
static void scenery(const struct track_cam *cam, float cs, float sn)
{
    for (int i = 0; i < s2anim_rows() && i < ANIMS; i++) {
        int x, y, id;
        if (!s2anim_heard(i, &x, &y, &id) || id < S2SND_AMBIENT_FIRST || id - S2SND_AMBIENT_FIRST >= S2SND_AMBIENT || !amb[id - S2SND_AMBIENT_FIRST].len) {
            if (scene[i].id >= 0) AU_STOP = 1u << (V_ANIM + i);
            scene[i].id = -1;
            continue;
        }
        const struct s2snd_sample *s = &amb[id - S2SND_AMBIENT_FIRST];
        float dx = (float)x - cam->x, dy = (float)y - cam->y, d = sqrtf(dx * dx + dy * dy);
        struct s2snd_heard h;
        if (scene[i].last < 0.0f) scene[i].last = d;
        s2snd_heard(s, 1.0f, d, scene[i].last, dx * cs + dy * sn, S2ANIM_RANGE, DOPPLER, &h);
        scene[i].last = d;
        uint32_t step = s2snd_step(h.rate), reg = s2snd_vol_reg(h.vol, h.pan);
        if (scene[i].id != id) {
            scene[i].id = id;
            play_sample(V_ANIM + i, s, step, at_level(reg));
        } else {
            if (step != scene[i].step) AU_STEP(V_ANIM + i) = step;
            if (reg != scene[i].reg) AU_VOL(V_ANIM + i) = at_level(reg);
        }
        scene[i].step = step;
        scene[i].reg = reg;
    }
}

void s2snd_frame(const struct track_cam *cam, const struct track_cam *cam2)
{
    if (!active) return;
    uint32_t t0 = rdcycle();
    uint32_t vf = VID_FRAME(VID_STATUS), dvf = frames ? (vf - last_vf) & 0xFFFF : 1;
    last_vf = vf;
    if (dvf > 10) dvf = 10;
    struct s2car_state *p = race_car(0);
    if (ready) {
        float cs = cosf(cam->yaw), sn = sinf(cam->yaw);     /* the listener's left, in x and y (the camera's heading - pi: 0xaf338) */
        if (cam2) {                                         /* combat mode: every car but the first is heard from the second view's camera (0x1fe18) */
            const float cs2 = cosf(cam2->yaw), sn2 = sinf(cam2->yaw);
            engine(0, cam, cs, sn);
            for (int i = 1; i < race_cars() && i < RACE_CARS; i++) engine(i, cam2, cs2, sn2);
            tyres(1, race_car(1));
        } else {
            for (int i = 0; i < race_cars() && i < RACE_CARS; i++) engine(i, cam, cs, sn);
        }
        tyres(0, p);
        scenery(cam, cs, sn);
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
        if (s2props_hit_take()) knock(S2SND_CRASH + 1, 5u);     /* the player's car knocked an obstacle (0x2a639 starts sample 0x10, the middle one of the crashes') */
        ground_before = p->on_ground;
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

void s2sound_menu(int id)
{
    if (!inited || id < 0 || id >= S2SND_MENU_SAMPLES || !menu_smp[id].len) return;
    const struct s2snd_sample *s = &menu_smp[id];
    play_sample(V_MENU, s, s2snd_step(s->rate), s2snd_vol_reg(s->vol, 0x8000));
}

void s2sound_effects(int32_t f)
{
    const uint32_t k = f < 0 ? 0 : f > 0x8000 ? 0x8000 : (uint32_t)f, before = fx_level;
    fx_level = k;                                           /* (the writes of the frames that follow are at this level too) */
    if (!active) return;
    if (before == 0x8000 && k < 0x8000) AU_STOP = 0xFu << V_KNOCK;     /* (a knock is over in a moment: the ones playing go at the first step down, the later ones start at the level) */
    for (int i = 0; i < RACE_CARS; i++)
        if (eng[i].id >= 0) AU_VOL(V_ENGINE + i) = at_level(eng[i].reg);
    for (int p = 0; p < 2; p++)
        if (tyre[p].id >= 0) AU_VOL(tyre_voice[p]) = at_level(tyre[p].reg);
    for (int i = 0; i < ANIMS; i++)
        if (scene[i].id >= 0) AU_VOL(V_ANIM + i) = at_level(scene[i].reg);
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

int s2snd_racing(void)
{
    return active;
}

void s2snd_stop(void)
{
    if (!active) return;
    active = 0;
    AU_STOP = ALL_VOICES;
    music_stop();                               /* a read of the ring may be running: the track's load wants the slot */
    if (frames >= 50 && !cost_logged) log_cost();
}
