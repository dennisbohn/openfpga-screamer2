#include "music.h"
#include "hw.h"
#include "dlog.h"
#include "slot.h"

#define CUE_SLOT    21                      /* GAME.DAT, core/data.json */
#define DISC_SLOT   22                      /* GAME.GOG */
#define RING_MEM    (SDRAM_BASE + 0x1E00000)    /* the ring: 256 KB right after SOUND.DAT's 2 MB (s2sound.c) */
#define CHUNK       0x8000u                 /* a read: 0.19 s of music, 25 ms at the APF's 1.3 MB/s */
#define MARGIN      0x400u                  /* left free behind the mixer's place (it holds 64 bytes) */
#define MASTER      0x4000                  /* the CD master volume at the game's 0x6000 (music.h) */
#define SECTOR      2352u
#define MAX_TRACKS  99
#define PRIME       0x18000u                /* the mixer starts when this many bytes are in the ring: 3 reads, 0.55 s of music */
#define KEEP        (CPU_HZ * 3 / 10)       /* cycles of song a blocking read must leave in the ring besides what it and the work on its bytes take: 0.3 s, six frames' work */
#define UNSERVED    (CPU_HZ / 2)            /* cycles a song may go without music_frame() while blocking calls follow each other (a load made of many reads): 0.5 s, the work after the last of them is on top */
#define FRAME_CYCLES (CPU_HZ / 44100)       /* cycles a ring frame plays for (the CD's 44,100 a second) */
#define READ_CYCLES 100u                    /* cycles a byte of a slot read may take: 0.74 MB/s, below anything measured (2.4 MB in 2.1 s on the Pocket, 32 KB in 25 ms) */

static int32_t first[MAX_TRACKS + 2];       /* per CD track its first sector (INDEX 01), -1 none */
static uint8_t audio[MAX_TRACKS + 2];       /* 1 for an AUDIO track */
static uint32_t sectors;                    /* the image's */

static int playing;                         /* the CD track playing, 0 none */
static int paused;                          /* the track music_pause stopped, 0 none */
static uint32_t paused_at;                  /* its place: bytes into it, whole sectors */
static uint32_t master = MASTER;            /* music_master */
int music_on = 1;
static uint32_t from, to, at;               /* its bytes in the image; the next byte to read */
static uint32_t written, played, place;     /* frames into the ring, frames played, the mixer's place */
static uint32_t reading;                    /* bytes of the read running, 0 none */
static uint32_t short_frames;               /* frames in which the mixer had less than 16 frames ahead */
static uint32_t origin;                     /* the byte of the song that the ring's frame 0 holds (a song resumed does not begin at its start) */
static uint32_t served;                     /* rdcycle() at the last music_frame(): the ring was looked after then */
static int priming;                         /* the first reads are coming in: the mixer starts when PRIME bytes are there */
static int held;                            /* a long read took the data slot: the song waits (music_pause) for the next music_frame() */
static uint32_t held_at, holds;             /* when it was held; how many times so far */

/* the text's next line from *p, without its line break: 0 at the end */
static int line(const char **p, const char *end, const char **s, const char **e)
{
    if (*p >= end) return 0;
    *s = *p;
    while (*p < end && **p != '\n')
        (*p)++;
    *e = *p;
    if (*p < end) (*p)++;
    return 1;
}

/* the line's next token from *p (spaces, tabs and CRs between): its length, 0 at the end */
static uint32_t token(const char **p, const char *end, const char **t)
{
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r'))
        (*p)++;
    *t = *p;
    while (*p < end && **p != ' ' && **p != '\t' && **p != '\r')
        (*p)++;
    return (uint32_t)(*p - *t);
}

static int is(const char *t, uint32_t n, const char *word)
{
    uint32_t i = 0;
    while (i < n && word[i] && t[i] == word[i])
        i++;
    return i == n && !word[i];
}

static uint32_t dec(const char *t, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n && t[i] >= '0' && t[i] <= '9'; i++)
        v = v * 10 + (uint32_t)(t[i] - '0');
    return v;
}

/* the cue sheet: TRACK nn type, INDEX 01 mm:ss:ff (75 frames, that is sectors, a second) */
static void cue(const char *p, const char *end)
{
    const char *s, *e, *t;
    uint32_t cur = 0;
    while (line(&p, end, &s, &e)) {
        uint32_t n = token(&s, e, &t);
        if (is(t, n, "TRACK")) {
            n = token(&s, e, &t);
            cur = dec(t, n);
            if (cur > MAX_TRACKS) cur = 0;
            n = token(&s, e, &t);
            audio[cur] = (uint8_t)is(t, n, "AUDIO");
        } else if (is(t, n, "INDEX") && cur) {
            n = token(&s, e, &t);
            if (dec(t, n) != 1) continue;
            n = token(&s, e, &t);
            uint32_t f[3] = { 0, 0, 0 }, k = 0;
            for (uint32_t i = 0; i < n && k < 3; i++) {
                if (t[i] == ':') k++;
                else f[k] = f[k] * 10 + (uint32_t)(t[i] - '0');
            }
            first[cur] = (int32_t)((f[0] * 60 + f[1]) * 75 + f[2]);
        }
    }
}

static void slot_guard(uint32_t len);

int music_init(void)
{
    uint32_t size = slot_size(CUE_SLOT);
    int n = 0;
    sectors = slot_size(DISC_SLOT) / SECTOR;
    for (int i = 0; i <= MAX_TRACKS; i++) {
        first[i] = -1;
        audio[i] = 0;
    }
    if (size && size <= 4096 && sectors && slot_read(CUE_SLOT, 0, RING_MEM, SLOT_REST) == 0) {
        cue((const char *)RING_MEM, (const char *)RING_MEM + size);
        for (int i = 1; i <= MAX_TRACKS; i++)
            n += audio[i] && first[i] >= 0 && (uint32_t)first[i] < sectors;
    }
    if (!n) dlog_str("music: no audio tracks (GAME.GOG, GAME.DAT)\n");     /* (the log has 4 KB) */
    slot_hook = slot_guard;                     /* from now on every blocking slot call looks after the ring first */
    return n != 0;
}

/* the next read into the ring, if there is room for it: its bytes, else 0 */
static uint32_t next(void)
{
    uint32_t off = written * 4 % AU_MUSIC_RING, len = CHUNK;
    uint32_t room = AU_MUSIC_RING - MARGIN - (written - played) * 4;
    if (len > AU_MUSIC_RING - off) len = AU_MUSIC_RING - off;
    if (len > to - at) len = to - at;
    return len <= room ? len : 0;
}

/* a read of len bytes is done, with the APF's result r */
static void done(uint32_t len, int r)
{
    if (r) {
        AU_MUSIC = 0;
        playing = priming = 0;
        dlog_str("music: a read of GAME.GOG failed (result ");
        dlog_dec(r);
        dlog_str("), it stops\n");
        return;
    }
    written += len / 4;
    at += len;
    if (at == to) at = from;                /* the song again, as the drive's poll plays it */
}

/* a read of the ring done, if the APF is through with it: taken into the ring's state. wait: until it is. */
static void take_read(int wait)
{
    if (!reading) return;
    if (wait) {
        uint32_t t0 = rdcycle();
        while (slot_busy() && rdcycle() - t0 < 20u * CPU_HZ)
            ;
    } else if (slot_busy()) {
        return;
    }
    uint32_t len = reading;
    reading = 0;
    done(len, slot_result());
}

/* the next read into the ring started, if there is room for it (the CPU only starts it: the APF writes the SDRAM while the frame is drawn) */
static void top_up(void)
{
    uint32_t len = next();
    if (len && !reading) {
        slot_start(DISC_SLOT, at, RING_MEM + written * 4 % AU_MUSIC_RING, len);
        reading = len;
    }
}

/* CD track n from byte off of it on (a whole sector), the music before stopped. The ring is filled over the next frames by music_frame():
   the mixer is off until PRIME bytes are in, so no frame is long (the 224 KB of a blocking fill took 170 ms) and the mixer never plays
   what the ring held before. */
static void cd_start(int n, uint32_t off)
{
    int m = n + 1;
    music_stop();
    if (!music_on || n < 1 || n > MAX_TRACKS || !audio[n] || first[n] < 0 || (uint32_t)first[n] >= sectors) return;
    while (m <= MAX_TRACKS && first[m] < 0)
        m++;
    uint32_t last = m <= MAX_TRACKS && (uint32_t)first[m] < sectors ? (uint32_t)first[m] : sectors;
    from = (uint32_t)first[n] * SECTOR;
    to = last * SECTOR;
    at = off < to - from ? from + off : from;
    origin = at - from;
    written = played = place = short_frames = 0;
    playing = n;
    priming = 1;
    served = rdcycle();
    top_up();
}

void music_cd(int n)
{
    music_stop();
    if (music_on && n >= 2) cd_start(n, 0);
}

void music_pause(void)
{
    int n = playing;
    if (!n) return;
    if (!priming) played += ((AU_MUSIC & 0xFFFF) - place) & 0xFFFF;     /* (the mixer has not started yet while priming: nothing was played) */
    uint32_t off = (origin + played * 4) % (to - from) / SECTOR * SECTOR;
    music_stop();
    paused = n;
    paused_at = off;
}

void music_resume(void)
{
    if (paused) cd_start(paused, paused_at);
}

void music_fade(int32_t f)
{
    AU_MUSIC_VOL = master * (uint32_t)(f < 0 ? 0 : f > 0x8000 ? 0x8000 : f) >> 15;
}

void music_master(int32_t v)
{
    master = (uint32_t)(v < 0 ? 0 : v > 0x7FFF ? 0x7FFF : v) * MASTER / 0x6000;
}

int music_disc(int *lo, int *hi)
{
    *lo = *hi = 0;
    for (int i = 1; i <= MAX_TRACKS; i++)
        if (audio[i] && first[i] >= 0 && (uint32_t)first[i] < sectors) {
            if (!*lo) *lo = i;
            *hi = i;
        }
    return playing;
}

void music_frame(void)
{
    if (held) {                             /* the load that held the song is over: the caller looks after the ring again */
        held = 0;
        if (holds <= 8) {
            dlog_str("music: held for ");
            dlog_dec((int32_t)((rdcycle() - held_at) / (CPU_HZ / 1000)));
            dlog_str(" ms by a load\n");
        }
        music_resume();
    }
    if (!playing) return;
    served = rdcycle();
    if (!priming) {
        uint32_t p = AU_MUSIC & 0xFFFF;
        played += (p - place) & 0xFFFF;
        place = p;
    }
    take_read(0);
    if (!playing) return;
    if (!priming) short_frames += (int32_t)(written - played) < 16;
    top_up();
    if (priming && written * 4 >= PRIME) {  /* enough of the song is in: the mixer plays the ring from its start */
        priming = 0;
        AU_MUSIC_ADDR = RING_MEM - SDRAM_BASE;
        AU_MUSIC_VOL = master;
        AU_MUSIC = 1;
    }
}

int music_active(void)
{
    return playing || held;
}

/* Every blocking call of the data slot (slot.c) comes here first. The slot does one read at a time, so the ring's own read running is
   waited for (a few ms) and taken. A song that plays is held when the ring would not outlast the call: its estimated time (READ_CYCLES a
   byte) and the CPU's work on the bytes after it (as long again, the Pocket's logs: a room's or a track's build takes as long as its reads;
   the estimate is slower than the APF, which covers it) and KEEP besides. The lead is the live one (what is written less what the mixer has
   played): 1.2 to 1.45 s right after music_frame(), 0.55 s right after a start, so a picture or a sprite sheet (64 KB, 90 ms) passes
   always and a container of 0.4 MB only on a full ring. When the ring runs dry the mixer plays the old ring again; that is what this
   prevents. A chain of calls that each pass (the 64 KB chunks of a ZIP's entry with the inflating between them) is held when UNSERVED
   has gone by since music_frame(): the work after the last call, a build of the options room, must find the ring with 0.7 s in it
   at the least or the song held. A song held goes on from where it was at the next music_frame() (music_resume). While it is only starting nothing is
   played and a long read only delays it. len: the bytes the call reads (0: a look-up), SLOT_REST counts as long. */
static void slot_guard(uint32_t len)
{
    if (!playing) return;
    take_read(1);
    if (!playing || priming) return;        /* (the mixer has not started: a long read only delays the song) */
    uint32_t p = AU_MUSIC & 0xFFFF;
    int32_t ahead = (int32_t)(written - (played + ((p - place) & 0xFFFF)));     /* the frames the mixer has not played yet */
    uint32_t lead = ahead > 0 ? (uint32_t)ahead * FRAME_CYCLES : 0;
    uint64_t need = KEEP + (uint64_t)len * READ_CYCLES * 17 / 10;
    if (rdcycle() - served >= UNSERVED || lead < need) {
        held_at = rdcycle();
        holds++;
        music_pause();
        held = 1;
    }
}

void music_stop(void)
{
    AU_MUSIC = 0;
    if (reading) {                          /* (a read of the ring is waited for; its result does not matter any more) */
        uint32_t t0 = rdcycle();
        while (slot_busy() && rdcycle() - t0 < 20u * CPU_HZ)
            ;
        reading = 0;
    }
    if (playing && short_frames) {
        dlog_str("music: CD track ");
        dlog_dec(playing);
        dlog_str(" ran short of data in ");
        dlog_dec((int32_t)short_frames);
        dlog_str(" frames\n");
    }
    playing = paused = held = priming = 0;
}
