/*
 * A model of the hardware the music ring (firmware/music.c) lives on, for the PC: the APF's data slot as the CPU sees it (DS_* of hw.h:
 * one read at a time, a rate, a latency), the mixer's music ring (AU_MUSIC*: it plays the ring round and round from its start at 44,100
 * frames a second whatever is in it) and the CPU's clock, all in simulated time. The clock only moves where a test moves it (mhw_burn(): the
 * CPU works) and a little with every read of rdcycle(), so a loop that waits for the APF ends.
 *
 * The model knows what the ring should hold: every frame the APF writes carries the frame's place in the CD image (a tag), and the mixer, each
 * frame it plays, checks that the ring's frame is the one after the last it played (the song goes on, wrapping at the track's end). A frame that is
 * not (the ring ran dry and plays what it held before, or the mixer started on an empty ring) is counted in bad_frames. A slot call while
 * another read runs (a start, or one of the slot's registers written) is counted in overlaps. Both must stay 0.
 *
 * The test's copy of hw.h has the registers named above go through model_reg() and rdcycle() through mhw_cycle()
 * (tools/music_model.sh, tools/menu_screens.sh).
 */
#pragma once
#include <stdint.h>

struct mhw_stats {
    uint32_t overlaps;                  /* slot starts or register writes while a read ran */
    uint32_t bad_frames, bad_runs;      /* ring frames played that were not the song's next, in how many runs */
    uint32_t played;                    /* ring frames played in all */
    uint32_t starts, stops;             /* AU_MUSIC written 1, 0 */
    uint32_t ring_reads, ring_bytes;    /* the APF's transfers of the CD image (slot 22) */
    uint32_t reads, bytes, longest;     /* the others (the ZIP's): how many, how many bytes, the longest one in bytes */
    uint32_t overruns;                  /* a read landed outside the ring, or on frames the mixer had not played yet */
    uint32_t last_start, last_stop;     /* the song frame (absolute) the mixer began with at its last start, and had reached at its last stop */
    uint32_t first_bad_ms;              /* the time of the first bad frame, in ms of simulated time, 0 none */
};
extern struct mhw_stats mhw;

/* The CD image: tracks 2 .. 9 are audio, track t is mhw_track_sectors(t) long; the cue sheet of slot 21 and the image of slot 22 follow from that */
#define MHW_TRACK_SECTORS   (75u * 200u)        /* 200 s a track */
#define MHW_FIRST_TRACK     2
#define MHW_TRACKS          9

void mhw_reset(void);                   /* time 0, everything idle, the counters 0 */
void mhw_burn(uint64_t cycles);         /* the CPU works for so long (the mixer plays meanwhile) */
void mhw_burn_ms(uint32_t ms);
uint64_t mhw_now(void);                 /* the clock in cycles */
uint32_t mhw_cycle(void);               /* rdcycle() */
void mhw_rate(double bytes_per_second); /* the APF's rate (default 1.0 MB/s, 2 ms before the first byte) */
volatile uint32_t *model_reg(uint32_t addr);

extern int mhw_trace;                   /* 1: every bad frame (the first ten) is printed with what the ring held */
int mhw_mixer_on(void);
uint32_t mhw_mixer_song_frame(void);    /* the song frame (absolute, in the image) the mixer plays next */
uint32_t mhw_busy(void);                /* a transfer is running */
uint32_t mhw_lead(void);                /* the frames the ring holds ahead of the mixer that are the song's next, 0 when the mixer is off */
uint32_t mhw_image_frame(int track, uint32_t sector);   /* the image frame of a sector of a track */

/* the slot interface music.c calls, as slot.c has it; slot_read there is the real one, the tests use theirs */
uint32_t mhw_slot_size(uint32_t id);
