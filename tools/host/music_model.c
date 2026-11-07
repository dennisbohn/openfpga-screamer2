/*
 * The music ring against the loads, on the PC: the firmware's own music.c and slot.c on a model of the APF's data slot, the mixer's
 * ring and the CPU's clock in simulated time (music_hw.c). Scenarios of the menus' loads (short reads that must not chop the song, long
 * ones that must hold it, a chain of a ZIP's chunks with the CPU working between them, a song held again and again, a race starting while
 * a read of the ring runs, ...), a random mix of them, and negative controls (the guard left out, the CPU away for two seconds): the
 * model must see the repeat and the overlap there, or its zeros mean nothing.
 *
 * What is checked, always: the mixer never plays a ring frame that is not the song's next (bad_frames 0), no slot call meets a read
 * that runs (overlaps 0), no read lands outside the ring or on frames not yet played (overruns 0); and per scenario what the scenario
 * is about (held or not, how soon the song is audible, where it goes on).
 *
 * Usage: music_model   (exit code 0 if every test passed)
 */
#define _GNU_SOURCE
#include "../music.h"
#include "../slot.h"
#include "../dlog.h"
#include "../hw.h"
#include "music_hw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* what music.c links to and the song does not need: no GEN.DAT (the FM songs are stubbed in music_hw.c) */
struct dat_entry;
int dat_find(const uint8_t *d, uint32_t n, const char *name, struct dat_entry *e) { (void)d; (void)n; (void)name; (void)e; return 0; }
uint32_t dat_extract(const uint8_t *d, uint32_t n, const struct dat_entry *e, uint8_t *out, uint32_t max) { (void)d; (void)n; (void)e; (void)out; (void)max; return 0; }

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MS          (74250000ull / 1000)
#define SCRATCH     0x12000000u         /* a place for the ZIP's reads to go (the model does not write it) */
#define SECT_FRAMES 588                 /* ring frames in a sector of the image */

/* ------------------------------------------------------------------------------------------------ the machine as the page code drives it */

static uint32_t worst_frame_cycles;     /* the longest music_frame() in simulated cycles: it only looks and starts */

/* one video frame of a page: music_frame() first (s2page_run_step), then the page's own work and the wait for the swap fill the 20 ms */
static void frame(void)
{
    uint64_t t0 = mhw_now();
    music_frame();
    uint64_t used = mhw_now() - t0;
    if (used > worst_frame_cycles) worst_frame_cycles = (uint32_t)used;
    if (used < 20 * MS) mhw_burn(20 * MS - used);
}

static void frames(int n)
{
    while (n-- > 0) frame();
}

/* a blocking read of the ZIP (slot 3) of `bytes`, then `cpu_ms` of the CPU's work on it */
static void blocking(uint32_t bytes, uint32_t cpu_ms)
{
    CHECK(slot_read(3, 0, SCRATCH, bytes) == 0, "a blocking read of %u bytes failed", bytes);
    mhw_burn_ms(cpu_ms);
}

/* a load of `total` bytes in the ZIP's chunks of 64 KB with `cpu_ms` of inflating after each (zip.c's next_chunk) */
static void chunks(uint32_t total, uint32_t cpu_ms)
{
    for (uint32_t done = 0; done < total; done += 65536)
        blocking(total - done < 65536 ? total - done : 65536, cpu_ms);
}

static void begin_song(int track)
{
    music_on = 1;
    music_cd(track);
}

/* the model is clean: nothing the ring plays is wrong, nothing met on the slot */
static int clean(void)
{
    return mhw.bad_frames == 0 && mhw.overlaps == 0 && mhw.overruns == 0;
}

static void report(const char *name)
{
    CHECK(mhw.bad_frames == 0, "%s: the mixer played %u frames that were not the song's next, in %u runs (the first at %u ms)", name, mhw.bad_frames, mhw.bad_runs, mhw.first_bad_ms);
    CHECK(mhw.overlaps == 0, "%s: %u slot calls met a read that ran", name, mhw.overlaps);
    CHECK(mhw.overruns == 0, "%s: %u reads landed outside the ring or on frames not played yet", name, mhw.overruns);
}

static void fresh(void)
{
    music_stop();
    mhw_reset();
    music_init(0, 0);
    worst_frame_cycles = 0;
}

/* the song's place in the image, in frames from the track's first */
static uint32_t position(int track)
{
    return mhw_mixer_song_frame() - mhw_image_frame(track, 0);
}

/* frames until the mixer is on (at most `limit`); the number it took */
static int until_audible(int limit)
{
    int k = 0;
    while (!mhw_mixer_on() && k < limit) {
        frame();
        k++;
    }
    return k;
}

/* how far back the last hold put the song: the frames from where the mixer stopped to where it began again (a hold keeps the sector) */
static int32_t rewind_of_last_hold(void)
{
    return (int32_t)(mhw.last_stop - mhw.last_start);
}

/* ------------------------------------------------------------------------------------------------ the tests */

/* the start: the first reads come in over the frames, the mixer starts when 96 KB are there, and no frame waited for them */
static void test_start(void)
{
    fresh();
    begin_song(2);
    CHECK(music_active() && !mhw_mixer_on(), "start: the song is on its way and the mixer is not started yet");
    int k = until_audible(30);
    CHECK(mhw_mixer_on(), "start: the mixer did not start in 30 frames");
    CHECK(k <= 10, "start: the song was audible after %d frames (0.2 s)", k);
    CHECK(mhw.last_start == mhw_image_frame(2, 0), "start: the mixer starts at frame %u, not at the song's start", mhw.last_start);
    uint32_t lead = 0xFFFFFFFFu;
    for (int i = 0; i < 400; i++) {
        frame();
        uint32_t l = mhw_lead();
        if (i > 12 && l < lead) lead = l;
    }
    CHECK(lead >= 40000, "start: the ring held only %u frames (%.2f s) ahead of the mixer at the least", lead, lead / 44100.0);
    CHECK(mhw.starts == 1 && mhw.stops == 0, "start: the mixer was started %u times and stopped %u", mhw.starts, mhw.stops);
    CHECK(worst_frame_cycles < 1 * MS, "start: a frame waited %.2f ms in music_frame()", (double)worst_frame_cycles / (double)MS);
    printf("music_model: start: audible after %d frames (a whole fill of 224 KB would have been one frame of 170 ms), the ring %.2f s ahead at the least, the longest music_frame %.3f ms\n",
           k, lead / 44100.0, (double)worst_frame_cycles / (double)MS);
    report("start");
}

/* short reads (a page's picture, a sprite sheet: tens of ms, up to 250 KB) in the middle of the song do not chop it */
static void test_short(void)
{
    fresh();
    begin_song(2);
    frames(30);
    uint32_t stops = mhw.stops, sizes[] = { 2000, 20000, 60000, 120000, 250000 };
    for (int rep = 0; rep < 8; rep++)
        for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; s++) {
            blocking(sizes[s], 3);                  /* a read in the middle of a page's frame, the CPU a moment on it */
            CHECK(music_active() && mhw_mixer_on(), "short: a read of %u bytes stopped the song", sizes[s]);
            frames(6);
        }
    CHECK(mhw.stops == stops, "short: the mixer was stopped %u times by short reads", mhw.stops - stops);
    printf("music_model: short reads: 40 reads of 2 to 250 KB, the song never stopped\n");
    report("short");
}

/* one read (the options room's container, 0.9 MB; the desk scene's 1.1 MB): held or not as its size says; held, the song goes on from where
   it was held when the page is up, and nothing is played from the old ring or read on top of a read */
static void test_read(uint32_t bytes, uint32_t cpu_ms, int expect_hold)
{
    fresh();
    begin_song(2);
    frames(60);
    uint32_t t0 = (uint32_t)(mhw_now() / MS), stops = mhw.stops;
    blocking(bytes, cpu_ms);
    uint32_t load_ms = (uint32_t)(mhw_now() / MS) - t0;
    if (!expect_hold) {
        CHECK(mhw_mixer_on() && mhw.stops == stops, "read %u: a read the ring outlasts held the song", bytes);
        frames(60);
        printf("music_model: read %u KB + %u ms CPU (%u ms): not held, the ring outlasted it\n", bytes / 1024, cpu_ms, load_ms);
        report("read");
        return;
    }
    CHECK(!mhw_mixer_on() && mhw.stops == stops + 1, "read %u: the song was not held (mixer %d)", bytes, mhw_mixer_on());
    CHECK(music_active(), "read %u: the song is not waiting for the page", bytes);
    int k = until_audible(30);
    CHECK(mhw_mixer_on(), "read %u: the song did not go on", bytes);
    CHECK(k <= 10, "read %u: the song took %d frames to be audible again", bytes, k);
    int32_t back = rewind_of_last_hold();
    CHECK(back >= 0 && back < SECT_FRAMES, "read %u: the song goes on %d frames before where it was held (a sector is %d)", bytes, back, SECT_FRAMES);
    printf("music_model: read %u KB + %u ms CPU: held for %u ms, audible again %d frames after the page is up, %d frames back (a sector is %d)\n",
           bytes / 1024, cpu_ms, load_ms, k, back, SECT_FRAMES);
    frames(200);
    report("read");
}

/* a load made of the ZIP's chunks with the CPU working between them: 25 x 64 KB, 40 ms of inflating after each */
static void test_chain(void)
{
    fresh();
    begin_song(2);
    frames(40);
    uint32_t stops = mhw.stops;
    chunks(25 * 65536, 40);
    CHECK(mhw.stops == stops + 1 && !mhw_mixer_on(), "chain: the song was held %u times", mhw.stops - stops);
    frames(40);
    CHECK(mhw_mixer_on(), "chain: the song did not go on");
    printf("music_model: chain of 25 x 64 KB with 40 ms between: held once, on again\n");
    report("chain");
}

/* a load of the size the options room has, with the song playing 0.2 s, 0.45 s and 0.6 s into it: where the hold falls */
static void test_chain_hold_time(void)
{
    fresh();
    begin_song(2);
    frames(40);
    uint64_t t0 = mhw_now();
    uint32_t n = 0;
    while (mhw_mixer_on()) {
        blocking(65536, 40);
        n++;
    }
    uint32_t ms = (uint32_t)((mhw_now() - t0) / MS);
    CHECK(ms >= 350 && ms <= 750, "chain: the song was held %u ms into the load (after %u chunks)", ms, n);
    printf("music_model: a chain of 64 KB chunks with 40 ms of work after each: the song is held after %u chunks, %u ms into the load\n", n, ms);
    chunks(10 * 65536, 40);
    frames(30);
    report("chain-time");
}

/* the song is held while its first reads are coming in: nothing was played, it goes on from its start */
static void test_prime_hold(void)
{
    fresh();
    begin_song(2);
    blocking(900000, 700);                          /* the options room's load, at once: the first read of the song may be in flight */
    CHECK(!mhw_mixer_on(), "prime: the mixer had started");
    until_audible(30);
    CHECK(mhw_mixer_on(), "prime: the song did not go on");
    CHECK(mhw.last_start == mhw_image_frame(2, 0), "prime: the song went on at frame %u, not from its start", mhw.last_start);
    frames(100);
    report("prime");
    printf("music_model: held while priming: on again from its start\n");
}

/* a song held again and again (every load of a menu session) goes on from where it was each time: the place is the song's, not the ring's */
static void test_drift(void)
{
    fresh();
    begin_song(2);
    until_audible(30);
    frames(40);
    int64_t rewound = 0;
    int worst = 0;
    for (int i = 0; i < 12; i++) {
        blocking(1000000 + 50000 * (uint32_t)(i % 3), 500);
        frames(2 + i % 4);
        until_audible(30);
        int32_t back = rewind_of_last_hold();
        if (back > worst) worst = back;
        CHECK(back >= 0 && back < SECT_FRAMES, "drift: hold %d: the song went on %d frames before where it was held", i, back);
        rewound += back;
        frames(20 + 3 * i);
    }
    /* the whole accounting at once: the song's place is where the mixer began plus the frames it played since, less what the holds rewound */
    const uint32_t place = mhw_mixer_song_frame();
    const int64_t expect = (int64_t)mhw_image_frame(2, 0) + (int64_t)mhw.played - rewound;
    CHECK((int64_t)place == expect, "drift: the song is at frame %u, the frames played less the rewinds say %lld", place, (long long)expect);
    printf("music_model: 12 holds in a row: each put the song back by %d frames at the most (a sector is %d), %lld frames in all\n", worst, SECT_FRAMES, (long long)rewound);
    report("drift");
}

/* a race starts: another song begins while the menu's plays with a read of the ring in flight, or held; nothing meets, nothing old is played */
static void test_race_start(void)
{
    fresh();
    begin_song(2);
    for (int i = 0; i < 80; i++) {
        frame();
        if (mhw_busy() && i > 20) break;            /* a read of the ring is in flight */
    }
    music_stop();                                   /* load_track(): the menu's song stops, waiting for the read */
    CHECK(!mhw_mixer_on() && !music_active(), "race: the menu's song did not stop");
    blocking(2400000, 1700);                        /* the track's load */
    music_cd(7);                                    /* s2snd_music(): the track's song */
    CHECK(music_active(), "race: the track's song did not begin");
    until_audible(30);
    CHECK(mhw_mixer_on(), "race: the track's song is not audible");
    CHECK(mhw.last_start == mhw_image_frame(7, 0), "race: the song began at frame %u, not the track's start", mhw.last_start);
    frames(40);
    /* the song held by a load, and the race begins in that state */
    blocking(900000, 300);
    CHECK(!mhw_mixer_on(), "race: the song was not held");
    music_cd(5);
    CHECK(music_active() && !mhw_mixer_on(), "race: a held song and a new one");
    until_audible(30);
    CHECK(mhw_mixer_on() && mhw.last_start == mhw_image_frame(5, 0), "race: the new song is audible from its start (frame %u), not from the held one's place", mhw.last_start);
    music_stop();
    CHECK(!music_active() && !mhw_mixer_on(), "race: stopped");
    frames(5);
    CHECK(!music_active(), "race: music_frame() brought a stopped song back");
    report("race");
    printf("music_model: race start while the song plays, with a read in flight, and while it is held: ok\n");
}

/* the song reaches its end (200 s) and begins again, as the drive's poll plays it */
static void test_wrap(void)
{
    fresh();
    begin_song(2);
    frames(11000);                                  /* 220 s */
    uint32_t p = position(2);
    CHECK(p < 21 * 44100, "wrap: the song's place after 220 s is %u frames (%.1f s)", p, p / 44100.0);
    report("wrap");
    printf("music_model: 220 s of a 200 s song: wrapped at its end without a bad frame\n");
}

/* negative controls: the same things without the guard, and the CPU away longer than the ring lasts. The model must see them. */
static void test_controls(void)
{
    fresh();
    begin_song(2);
    frames(60);
    void (*hook)(uint32_t) = slot_hook;
    slot_hook = 0;                                  /* the guard left out */
    for (int i = 0; i < 30 && !mhw_busy(); i++) frame();    /* wait for a read of the ring to be in flight: the next slot call meets it */
    blocking(900000, 300);
    slot_hook = hook;
    CHECK(mhw.overlaps > 0 || mhw.bad_frames > 0, "control 1: a long read without the guard went unseen (overlaps %u, bad frames %u)", mhw.overlaps, mhw.bad_frames);
    printf("music_model: negative control, no guard, a 0.9 MB read: overlaps %u, bad frames %u (%u runs)\n", mhw.overlaps, mhw.bad_frames, mhw.bad_runs);

    fresh();
    begin_song(2);
    frames(60);
    slot_hook = 0;
    mhw_burn_ms(30);
    blocking(1800000, 0);                           /* ... and with no read of the ring in flight: it is the dry ring that shows */
    slot_hook = hook;
    frames(3);
    CHECK(mhw.bad_frames > 0, "control 2: a 1.8 MB read without the guard left the ring alone (bad frames %u)", mhw.bad_frames);
    printf("music_model: negative control, no guard, a 1.8 MB read: the mixer played the old ring, bad frames %u (%u runs)\n", mhw.bad_frames, mhw.bad_runs);

    fresh();
    begin_song(2);
    frames(60);
    mhw_burn_ms(2500);                              /* the CPU away for 2.5 s and no slot call: nothing can hold the song */
    frames(2);
    CHECK(mhw.bad_frames > 0, "control 3: two and a half seconds without music_frame() went unseen");
    printf("music_model: negative control, 2.5 s without a call: bad frames %u (%u runs): the limit of the guard is a stretch of the CPU without a slot call (the ring lasts 1.2 s)\n",
           mhw.bad_frames, mhw.bad_runs);

    fresh();
    begin_song(2);
    frames(60);
    mhw_burn_ms(1000);                              /* the same but 1 s: the ring still holds */
    frames(3);
    CHECK(clean(), "control 4: a second without music_frame() ran the ring dry (%u frames)", mhw.bad_frames);
    printf("music_model: 1 s without a call: the ring holds (the guard keeps 0.3 s of the ring for the work after a read and as much again as the read took)\n");
}

/* a random mix: frames, short and long reads, chains, CPU stretches, songs started, stopped and held, at the APF's rate or slower */
static uint32_t rnd_state;
static uint32_t rnd(uint32_t n)
{
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return (rnd_state >> 8) % n;
}

static void test_fuzz(int guard)
{
    uint32_t stops_total = 0, sessions = 0, ops_total = 0, bad_sessions = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        if (getenv("MHW_SEED") && (uint32_t)atoi(getenv("MHW_SEED")) != seed) continue;
        rnd_state = seed * 7919u;
        fresh();
        if (!guard) slot_hook = 0;                  /* (the negative control: the same sessions without the guard) */
        mhw_rate(seed % 3 == 0 ? 0.75e6 : seed % 3 == 1 ? 1.0e6 : 1.3e6);
        begin_song(2 + (int)rnd(8));
        char trace[16][48];
        int n = 0, ops = 300;
        while (ops--) {
            char *t = trace[n++ % 16];
            switch (rnd(10)) {
            case 0: case 1: case 2: case 3: {
                int f = 1 + (int)rnd(12);
                snprintf(t, 48, "frames %d", f);
                frames(f);
                break;
            }
            case 4: {
                uint32_t b = 1000 + rnd(250000), c = rnd(5);
                snprintf(t, 48, "read %u + %u ms", b, c);
                blocking(b, c);                     /* a page's picture or sprite sheet */
                break;
            }
            case 5: {
                uint32_t b = 300000 + rnd(1500000), c = rnd(150);
                snprintf(t, 48, "read %u + %u ms", b, c);
                blocking(b, c);                     /* a container */
                break;
            }
            case 6: {
                uint32_t k = 1 + rnd(16), c = rnd(80);
                snprintf(t, 48, "chunks %u x 64K + %u ms", k, c);
                chunks(65536 * k, c);
                break;
            }
            case 7: {
                uint32_t c = rnd(250);
                snprintf(t, 48, "cpu %u ms", c);
                mhw_burn_ms(c);                     /* the CPU on its own work, no slot call (and then the page's frame: stretches do not follow each other, that is what the guard assumes) */
                frame();
                break;
            }
            case 8:
                snprintf(t, 48, "song?");
                if (rnd(4) == 0) {
                    snprintf(t, 48, "song");
                    begin_song(2 + (int)rnd(8));
                }
                break;
            default:
                snprintf(t, 48, "stop?");
                if (rnd(8) == 0) {
                    snprintf(t, 48, "stop");
                    music_stop();
                } else {
                    frames(1);
                }
                break;
            }
            if (getenv("MHW_SEED")) printf("  %-28s t=%.3f s  active %d mixer %d lead %.2f s  bad %u overruns %u\n", t, (double)mhw_now() / 74250000.0, music_active(), mhw_mixer_on(), mhw_lead() / 44100.0, mhw.bad_frames, mhw.overruns);
            if (!clean()) break;
        }
        stops_total += mhw.stops;
        ops_total += 300 - (uint32_t)(ops + 1 > 0 ? ops + 1 : 0);
        sessions++;
        checks++;
        bad_sessions += !clean();
        if (!clean() && guard) {
            failures++;
            printf("FAIL: fuzz seed %u: bad frames %u (%u runs, the first at %u ms), overlaps %u, overruns %u; the last operations:\n", seed, mhw.bad_frames, mhw.bad_runs, mhw.first_bad_ms,
                   mhw.overlaps, mhw.overruns);
            for (int i = n > 16 ? n - 16 : 0; i < n; i++) printf("    %s\n", trace[i % 16]);
        }
    }
    if (guard) {
        printf("music_model: %u random sessions (%u operations) at 0.75, 1.0 and 1.3 MB/s: %u mixer stops in all, every session clean\n", sessions, ops_total, stops_total);
    } else {
        CHECK(bad_sessions >= sessions / 2, "control: only %u of the %u sessions without the guard went wrong", bad_sessions, sessions);
        printf("music_model: negative control, the same sessions without the guard: %u of %u went wrong (a repeat, or a slot call on a read)\n", bad_sessions, sessions);
    }
}

int main(void)
{
    int fd = memfd_create("sdram", 0);
    if (fd < 0 || ftruncate(fd, SDRAM_SIZE) ||
        mmap((void *)(uintptr_t)SDRAM_BASE, SDRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED ||
        mmap((void *)0xF0000000u, 0x4000000u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("map");
        return 1;
    }
    dlog_init();
    mhw_reset();
    CHECK(music_init(0, 0) == 1, "music_init: no audio tracks in the cue sheet");
    mhw_trace = getenv("MHW_TRACE") != 0;
    test_start();
    test_short();
    test_read(900000, 700, 1);
    test_read(1200000, 400, 1);
    test_read(1000000, 0, 1);
    test_read(450000, 0, 0);
    test_chain();
    test_chain_hold_time();
    test_prime_hold();
    test_drift();
    test_race_start();
    test_wrap();
    test_controls();
    test_fuzz(1);
    test_fuzz(0);
    printf(failures ? "music_model: %d of %d checks FAILED\n" : "music_model: all %d checks passed\n", failures ? failures : checks, checks);
    return failures != 0;
}
