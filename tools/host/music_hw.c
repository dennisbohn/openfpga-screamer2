/*
 * A model of the data slot, the mixer's music ring and the CPU's clock (music_hw.h)
 */
#include "music_hw.h"
#include "../slot.h"
#include <stdio.h>
#include <string.h>

/* what music.c links to and the song does not need: no FM songs */
int fm_init(const uint8_t *f, uint32_t s) { (void)f; (void)s; return 0; }
int fm_play(int n) { (void)n; return 0; }
void fm_frame(void) {}
void fm_stop(void) {}
void fm_pause(void) {}
void fm_resume(void) {}
void fm_master(int32_t v) { (void)v; }
void fm_fade(int32_t f) { (void)f; }
int fm_playing(void) { return 0; }
void fm_stats(uint32_t *a, uint32_t *b, uint32_t *c) { *a = *b = *c = 0; }

#define CPU_HZ      74250000ull
#define RING_FRAMES 65536u
#define RING_MEM    0x11E00000u                 /* SDRAM_BASE + 0x1E00000 (music.c) */
#define SECTOR      2352u
#define FRAMES_SECT (SECTOR / 4)
#define MIX_RATE    44100ull                    /* ring frames a second */
#define STATUS      0x80000000u                 /* set in a status register by the model: a CPU write clears it, which is how a write is seen */
#define INVALID     0xFFFFFFFFu

enum { R_DS_ID = 0x20 / 4, R_DS_OFFSET = 0x24 / 4, R_DS_ADDR = 0x28 / 4, R_DS_LENGTH = 0x2C / 4, R_DS_CTRL = 0x30 / 4, R_DS_SIZE = 0x34 / 4,
       R_AU_ADDR = 0x440C / 4, R_AU_VOL = 0x4410 / 4, R_AU_MUSIC = 0x4414 / 4 };

struct mhw_stats mhw;
int mhw_trace;

static uint64_t now;
static uint32_t regs[0x10000 / 4];
static double rate = 1.0e6;
static uint64_t latency = CPU_HZ / 500;         /* 2 ms to the first byte */

/* the APF */
static int busy;
static uint64_t done_at;
static uint32_t t_id, t_off, t_addr, t_len, snap[4];

/* the mixer */
static int mix_on;
static uint32_t mix_pos, mix_expect;
static uint64_t mix_t, mix_acc;
static int in_bad_run;
static uint32_t tag[RING_FRAMES];

/* the CD image */
static char cue_text[4096];
static uint32_t cue_len;

static uint32_t first_sector(int t)
{
    return t < MHW_FIRST_TRACK ? 0 : 1000u + (uint32_t)(t - MHW_FIRST_TRACK) * MHW_TRACK_SECTORS;
}

static uint32_t image_sectors(void)
{
    return first_sector(MHW_TRACKS) + MHW_TRACK_SECTORS;
}

uint32_t mhw_image_frame(int track, uint32_t sector)
{
    return (first_sector(track) + sector) * FRAMES_SECT;
}

static void make_cue(void)
{
    char *p = cue_text;
    p += sprintf(p, "FILE \"GAME.GOG\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n");
    for (int t = MHW_FIRST_TRACK; t <= MHW_TRACKS; t++) {
        uint32_t s = first_sector(t), sec = s / 75;
        p += sprintf(p, "  TRACK %02d AUDIO\r\n    INDEX 01 %02u:%02u:%02u\r\n", t, sec / 60, sec % 60, s % 75);
    }
    cue_len = (uint32_t)(p - cue_text);
}

/* the frame after f in the song: the track's own frames, round and round */
static uint32_t next_frame(uint32_t f)
{
    for (int t = MHW_FIRST_TRACK; t <= MHW_TRACKS; t++) {
        uint32_t a = first_sector(t) * FRAMES_SECT, b = (first_sector(t) + MHW_TRACK_SECTORS) * FRAMES_SECT;
        if (f >= a && f < b) return f + 1 == b ? a : f + 1;
    }
    return f + 1;
}

uint32_t mhw_slot_size(uint32_t id)
{
    return id == 21 ? cue_len : id == 22 ? image_sectors() * SECTOR : 0;
}

void mhw_serve_cue(void *to)
{
    memcpy(to, cue_text, cue_len);
}

uint64_t mhw_last_service;
uint32_t mhw_chopped;
uint32_t mhw_class_reads[MHW_CLASSES], mhw_class_holds[MHW_CLASSES], mhw_class_bytes[MHW_CLASSES];

const char *mhw_class_name(int c)
{
    static const char *const n[MHW_CLASSES] = { "up to 4 KB", "up to 64 KB", "up to 256 KB", "up to 1 MB", "over 1 MB" };
    return n[c];
}

static void play_frames(uint64_t frames)
{
    for (; frames; frames--) {
        uint32_t t = tag[mix_pos];
        if (t != mix_expect) {
            mhw.bad_frames++;
            if (!mhw.first_bad_ms) mhw.first_bad_ms = (uint32_t)(now / (CPU_HZ / 1000));
            if (mhw_trace && mhw.bad_frames <= 10) printf("  bad frame at %.1f ms: ring %u holds %d, the song's next is %d\n", (double)now / (CPU_HZ / 1000), mix_pos, (int)t, (int)mix_expect);
            if (!in_bad_run) mhw.bad_runs++;
            in_bad_run = 1;
            if (t != INVALID) mix_expect = t;       /* (what the ring holds is played; the count is of frames that are not the song's next) */
        } else {
            in_bad_run = 0;
        }
        mix_expect = next_frame(mix_expect);
        mix_pos = (mix_pos + 1) & (RING_FRAMES - 1);
        mhw.played++;
    }
}

static void mixer_to(uint64_t t)
{
    uint64_t dt = t - mix_t;
    mix_t = t;
    if (!mix_on) return;
    mix_acc += dt * MIX_RATE;
    play_frames(mix_acc / CPU_HZ);
    mix_acc %= CPU_HZ;
}

static void complete(void)
{
    busy = 0;
    if (t_id == 22) {
        uint32_t n = t_len / 4, idx = ((t_addr - RING_MEM) / 4) & (RING_FRAMES - 1);
        if (t_addr < RING_MEM || t_addr + t_len > RING_MEM + RING_FRAMES * 4 || (t_addr & 3)) mhw.overruns++;      /* it must land inside the ring (music.c wraps its reads) */
        if (mix_on && ((idx - mix_pos) & (RING_FRAMES - 1)) + n > RING_FRAMES) mhw.overruns++;          /* ... and not on frames the mixer has not played yet */
        for (uint32_t k = 0; k < n; k++) tag[(idx + k) & (RING_FRAMES - 1)] = t_off / 4 + k;
        mhw.ring_reads++;
        mhw.ring_bytes += t_len;
    } else {
        if (t_id == 21 && t_addr >= 0x10000000u) memcpy((void *)(uintptr_t)t_addr, cue_text, t_len < cue_len ? t_len : cue_len);
        mhw.reads++;
        mhw.bytes += t_len;
        if (t_len > mhw.longest) mhw.longest = t_len;
    }
}

static void start_transfer(void)
{
    if (busy) mhw.overlaps++;                   /* the slot does one read at a time (slot.h) */
    t_id = regs[R_DS_ID];
    t_off = regs[R_DS_OFFSET];
    t_addr = regs[R_DS_ADDR];
    t_len = regs[R_DS_LENGTH];
    if (t_len == 0xFFFFFFFFu) t_len = mhw_slot_size(t_id) - t_off;
    snap[0] = t_id;
    snap[1] = t_off;
    snap[2] = t_addr;
    snap[3] = regs[R_DS_LENGTH];
    busy = 1;
    done_at = now + latency + (uint64_t)((double)t_len * (double)CPU_HZ / rate);
}

static void mixer_start(void)
{
    mhw.starts++;
    mhw.last_start = tag[0];
    if (mhw_trace > 1) printf("  mixer start at %.3f s, ring frame 0 holds %d\n", (double)now / CPU_HZ, (int)tag[0]);
    mix_on = 1;
    mix_pos = 0;
    mix_acc = 0;
    in_bad_run = 0;
    mix_expect = tag[0];
    if (tag[0] == INVALID || regs[R_AU_ADDR] != RING_MEM - 0x10000000u) {
        mhw.bad_frames++;                       /* started on an empty ring or one that is not ours */
        mhw.bad_runs++;
        in_bad_run = 1;
        if (tag[0] == INVALID) mix_expect = INVALID;
    }
}

static void sync(void)
{
    if (!(regs[R_DS_CTRL] & STATUS)) {          /* the CPU wrote the control register: a start */
        if (regs[R_DS_CTRL] & 1) start_transfer();
    }
    if (!(regs[R_AU_MUSIC] & STATUS)) {         /* ... or the mixer's */
        mixer_to(now);
        if (regs[R_AU_MUSIC] & 1) mixer_start();
        else if (mix_on) {
            mhw.stops++;
            mhw.last_stop = mix_expect;
            if (mhw_trace > 1) printf("  mixer stop at %.3f s\n", (double)now / CPU_HZ);
            mix_on = 0;
        }
    }
    if (busy && done_at <= now) {               /* in time order: the mixer plays up to the moment the data lands, then on */
        mixer_to(done_at);
        complete();
    }
    mixer_to(now);
    if (busy && (regs[R_DS_ID] != snap[0] || regs[R_DS_OFFSET] != snap[1] || regs[R_DS_ADDR] != snap[2] || regs[R_DS_LENGTH] != snap[3])) {
        mhw.overlaps++;                         /* a register of the read that runs was written */
        snap[0] = regs[R_DS_ID];
        snap[1] = regs[R_DS_OFFSET];
        snap[2] = regs[R_DS_ADDR];
        snap[3] = regs[R_DS_LENGTH];
    }
    regs[R_DS_CTRL] = STATUS | (busy ? 1u : 0u);
    regs[R_DS_SIZE] = mhw_slot_size(regs[R_DS_ID]);
    regs[R_AU_MUSIC] = STATUS | mix_pos | (mix_on ? 1u << 16 : 0u);
}

volatile uint32_t *model_reg(uint32_t addr)
{
    static int spin;
    if (addr < 0xF0000000u || addr >= 0xF0010000u) return (volatile uint32_t *)(uintptr_t)addr;
    spin = addr == 0xF0000030u ? spin + 1 : 0;
    if (spin >= 4 && busy && done_at > now) now = done_at;      /* the control register read again and again: the CPU waits for the APF (the wait is jumped over) */
    sync();
    return &regs[(addr & 0xFFFFu) >> 2];
}

uint32_t mhw_cycle(void)
{
    now += 3;                                   /* a read of the counter costs a few cycles: a loop that waits for the APF ends */
    return (uint32_t)now;
}

uint64_t mhw_now(void)
{
    return now;
}

void mhw_zip_read(uint32_t len, uint32_t cpu_cycles_per_byte)
{
    sync();                                     /* (what the CPU wrote before is seen first: a stop of the mixer by music_stop() is not this read's) */
    uint32_t stops = mhw.stops, lead = mhw_trace ? mhw_lead() : 0;
    if (slot_hook) slot_hook(len);
    sync();
    if (busy) mhw.overlaps++;                   /* the slot is in use by a read of the ring: the guard should have waited for it */
    int c = len <= 4096 ? 0 : len <= 65536 ? 1 : len <= 262144 ? 2 : len <= 1048576 ? 3 : 4;
    mhw_class_reads[c]++;
    mhw_class_bytes[c] += len;
    mhw_class_holds[c] += mhw.stops != stops;
    if (mhw.stops != stops && len <= 65536 && now - mhw_last_service < CPU_HZ / 4) mhw_chopped++;
    if (mhw.stops != stops && mhw_trace) printf("  held by a read of %u bytes, %.0f ms after the last music_frame, the ring held %.2f s\n", len, (double)(now - mhw_last_service) / (CPU_HZ / 1000), lead / 44100.0);
    mhw.reads++;
    mhw.bytes += len;
    if (len > mhw.longest) mhw.longest = len;
    mhw_burn(latency + (uint64_t)((double)len * (double)CPU_HZ / rate) + (uint64_t)len * cpu_cycles_per_byte);
}

void mhw_burn(uint64_t cycles)
{
    now += cycles;
    sync();
}

void mhw_burn_ms(uint32_t ms)
{
    mhw_burn((uint64_t)ms * (CPU_HZ / 1000));
}

void mhw_rate(double bytes_per_second)
{
    rate = bytes_per_second;
}

int mhw_mixer_on(void)
{
    sync();
    return mix_on;
}

uint32_t mhw_mixer_song_frame(void)
{
    sync();
    return mix_expect;
}

uint32_t mhw_busy(void)
{
    sync();
    return busy;
}

uint32_t mhw_lead(void)
{
    sync();
    if (!mix_on) return 0;
    uint32_t e = mix_expect, n = 0;
    while (n < RING_FRAMES && tag[(mix_pos + n) & (RING_FRAMES - 1)] == e) {
        e = next_frame(e);
        n++;
    }
    return n;
}

void mhw_reset(void)
{
    memset(&mhw, 0, sizeof mhw);
    memset(mhw_class_reads, 0, sizeof mhw_class_reads);
    memset(mhw_class_holds, 0, sizeof mhw_class_holds);
    memset(mhw_class_bytes, 0, sizeof mhw_class_bytes);
    mhw_chopped = 0;
    memset(regs, 0, sizeof regs);
    memset(tag, 0xFF, sizeof tag);
    now = 1000;
    mix_t = now;
    busy = mix_on = in_bad_run = 0;
    mix_acc = 0;
    mix_pos = 0;
    rate = 1.0e6;
    make_cue();
    regs[R_DS_CTRL] = STATUS;
    regs[R_AU_MUSIC] = STATUS;
}
