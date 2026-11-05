/*
 * Time attack's ghost (s2ghost.h)
 */
#include "s2ghost.h"
#include "save.h"
#include "crc32.h"
#include <string.h>

_Static_assert(sizeof(struct s2gh_head) == S2GH_HEAD, "the head of a block");
_Static_assert(S2GH_TRACKS * S2GH_BLOCK <= SAVE_ATTACK_SIZE, "seven blocks in ATTACK.DAT");
_Static_assert(S2GH_BLOCK % 16 == 0, "blocks stay aligned");

#define BLOCK(n)    ((struct s2gh_head *)(uintptr_t)(SAVE_ATTACK_ADDR + (uint32_t)((n) - 1) * S2GH_BLOCK))

uint8_t s2gh_pack(const struct s2car_input *in, int manual)
{
    return (uint8_t)((in->throttle > 0.5f ? 1 : 0) | (in->brake > 0.5f ? 2 : 0) | (in->steer < -0.5f ? 4 : 0) | (in->steer > 0.5f ? 8 : 0) |
                     (in->handbrake ? 16 : 0) | (in->gear_up ? 32 : 0) | (in->gear_down ? 64 : 0) | (manual ? 128 : 0));
}

struct s2car_input s2gh_unpack(uint8_t b, int *manual)
{
    if (manual) *manual = (b & 128) != 0;
    return (struct s2car_input){ .throttle = (b & 1) ? 1.0f : 0.0f, .brake = (b & 2) ? 1.0f : 0.0f,
                                 .steer = ((b & 8) ? 1.0f : 0.0f) - ((b & 4) ? 1.0f : 0.0f), .gear_up = (b & 32) != 0, .gear_down = (b & 64) != 0,
                                 .handbrake = (b & 16) != 0 };
}

const uint8_t *s2gh_data(const struct s2gh_head *h)
{
    return (const uint8_t *)h + S2GH_HEAD;
}

/* the CRC-32 over bytes 8 .. 32 + steps */
static uint32_t crc_of(const struct s2gh_head *h)
{
    return crc32((const uint8_t *)h + 8, (const uint8_t *)h + S2GH_HEAD + h->steps);
}

const struct s2gh_head *s2gh_stored(int track)
{
    if (track < 1 || track > S2GH_TRACKS) return 0;
    const struct s2gh_head *h = BLOCK(track);
    if (memcmp(h->magic, "S2AT", 4) || h->version != 1 || h->track != track || h->steps == 0 || h->steps > S2GH_MAX) return 0;
    if (h->team < 1 || h->team > 4 || h->car < 1 || h->car > 5 || h->slot > 1 || h->damages > 1) return 0;
    if (!(h->time > 0.0f && h->time < 3600.0f)) return 0;
    return h->crc == crc_of(h) ? h : 0;
}

const struct s2gh_head *s2gh_ghost(int track, int damages)
{
    const struct s2gh_head *h = s2gh_stored(track);
    return h && h->sim == S2GH_SIM && h->damages == (damages != 0) ? h : 0;
}

/* ------------------------------------------------------------------------------------------------ the recording */

static struct {
    int track, team, car, slot, damages;
    uint32_t steps;
    int full;
    uint8_t data[S2GH_MAX];
} rec;

void s2gh_rec_begin(int track, int team, int car, int slot, int damages)
{
    rec.track = track;
    rec.team = team;
    rec.car = car;
    rec.slot = slot;
    rec.damages = damages != 0;
    rec.steps = 0;
    rec.full = 0;
}

void s2gh_rec_step(const struct s2car_input *in, int manual)
{
    if (rec.steps >= S2GH_MAX) {
        rec.full = 1;
        return;
    }
    rec.data[rec.steps++] = s2gh_pack(in, manual);
}

uint32_t s2gh_rec_steps(void)
{
    return rec.steps;
}

const uint8_t *s2gh_rec_data(void)
{
    return rec.data;
}

int s2gh_rec_full(void)
{
    return rec.full;
}

int s2gh_keep(float time, float best_lap, int laps)
{
    if (rec.full || rec.steps == 0 || rec.track < 1 || rec.track > S2GH_TRACKS || !(time > 0.0f && time < 3600.0f)) return 0;
    const struct s2gh_head *old = s2gh_stored(rec.track);
    if (old && !(time < old->time)) return 0;
    struct s2gh_head *h = BLOCK(rec.track);
    uint8_t *dst = (uint8_t *)h;
    memset(dst, 0, S2GH_HEAD);                              /* (a block half written is not whole: the head last) */
    memcpy(dst + S2GH_HEAD, rec.data, rec.steps);
    memcpy(h->magic, "S2AT", 4);
    h->version = 1;
    h->track = (uint8_t)rec.track;
    h->team = (uint8_t)rec.team;
    h->car = (uint8_t)rec.car;
    h->slot = (uint8_t)rec.slot;
    h->damages = (uint8_t)rec.damages;
    h->sim = S2GH_SIM;
    h->steps = rec.steps;
    h->time = time;
    h->best_lap = best_lap;
    h->laps = (uint32_t)laps;
    h->crc = crc_of(h);
    return 1;
}

void s2gh_erase(int track)
{
    if (track >= 1 && track <= S2GH_TRACKS) memset(BLOCK(track), 0, S2GH_BLOCK);
}
