/*
 * Host test of the race's sound rules (firmware/s2snd.c), run by tools/s2/check_sound.sh.
 *
 * 1. The firmware's reading of DATA/SOUND.DAT (GAME.DTR: the 33 samples, where their bytes are in the container,
 *    rate, volume, loop) and of CD_TRACKS in DATA/GAME.DAT is printed as text; tools/s2/check_sound.py prints the
 *    same from its own reading, the script compares the two.
 * 2. The rules are compared with the game's routines as OpenScreamer's notes (S2-5zf, port/src/s2_hooks.c
 *    FUN_000af168 = the game's 0xaf338) have them in double precision, read from the machine code (the constants were
 *    checked in STARTL.EXE: pow 2.0 at ds:0x237f, the Doppler 5.0 at 0x2387, 1.87 at 0x238b, 32767 at 0x2393, 32768
 *    at 0x239b, the engine's pitch 2.0 at 0x898): over a sweep of revs, distance, last distance and side the rate,
 *    volume and pan of s2snd_heard() may differ by one (float against double, a truncation falling on the other side),
 *    the tyres' rule and the knocks' thresholds are compared on a grid. Exit status 1 on a mismatch.
 *
 * Usage: check_sound <game folder>
 */
#include "../../firmware/s2snd.h"
#include "../../firmware/s2params.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *dir, const char *name, uint32_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if(!f) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 1);
    if(!p || fread(p, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    static uint32_t t[256];
    if(!t[1])
        for(uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for(int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    while(n--) c = t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static int bad;
#define CHECK(cond, ...) do { if(!(cond)) { if(bad++ < 20) { fprintf(stderr, "MISMATCH: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } } while(0)

/* the game's 0xaf338 as OpenScreamer has it (port/src/s2_hooks.c): rate, volume and pan of a sound heard from d */
static void ref_heard(const struct s2snd_sample *s, double pitch, float d, float last, float across, int range, double doppler,
                      int *rate, int *vol, int *pan)
{
    if(!((float)range > d)) { *rate = 11025; *vol = 0; *pan = 0x8000; return; }
    const int own = (int)((double)s->rate * (pitch * pitch));
    *rate = (int)((double)own + (double)own * (double)((last - d) / (float)range) * 5.0 * doppler);
    const double near_ = ((double)range - (double)d) / (double)range;
    *vol = (int)((double)s->vol * (near_ * near_));
    *pan = (int)(32768.0 - cos((1.0 - (double)across / (double)range) * 1.87) * 32767.0);
}

/* the tyres' rule as 0x1b36f has it (port/src/s2r_car.h) */
static int ref_tyres(int roll, int skid, float grip, float on_ground, float speed, int *vol)
{
    int squeal = 0;
    if(!(0.0f >= on_ground)) {
        if(grip >= 0.93) { }
        else {
            const long double q = (0.93 - (long double)grip) / 0.1;
            const double share = 1.0L >= q ? (double)q : 1.0;
            squeal = (int)(share * 29952.0);
        }
    }
    if(squeal == 0 || skid == 0) {
        if(roll == 0) return -1;
        const float v = speed < 50.0f ? speed : 50.0f;
        *vol = (long double)v * 655.0f >= 32767.0f ? 32767 : (int)((float)((long double)v * 655.0f));
        return 0x12 + roll;
    }
    *vol = squeal;
    return 7 + skid;
}

int main(int argc, char **argv)
{
    if(argc != 2) { fprintf(stderr, "usage: %s <game folder>\n", argv[0]); return 2; }
    uint32_t size, gsize;
    uint8_t *dat = slurp(argv[1], "DATA/SOUND.DAT", &size);
    uint8_t *game = slurp(argv[1], "DATA/GAME.DAT", &gsize);
    static struct s2snd_sample smp[S2SND_SAMPLES];
    int n = s2snd_parse(dat, size, 0, smp);
    printf("samples %d\n", n);
    for(int i = 0; i < S2SND_SAMPLES; i++)
        printf("sample %02x addr=%u len=%u rate=%u vol=%u loop=%d crc=%08x\n", i, smp[i].addr, smp[i].len, smp[i].rate, smp[i].vol,
               smp[i].loop, smp[i].len ? crc32(dat + smp[i].addr, smp[i].len) : 0u);
    for(int track = 1; track <= 7; track++) {                               /* the scenery's samples of every track */
        static struct s2snd_sample amb[S2SND_AMBIENT];
        s2snd_parse_ambient(dat, size, 0, track, amb);
        for(int i = 0; i < S2SND_AMBIENT; i++)
            printf("ambient %d %d addr=%u len=%u rate=%u vol=%u loop=%d crc=%08x\n", track, i, amb[i].addr, amb[i].len, amb[i].rate, amb[i].vol, amb[i].loop,
                   amb[i].len ? crc32(dat + amb[i].addr, amb[i].len) : 0u);
    }
    int cd[7] = { 0 };
    int ok = s2snd_cd_tracks(game, gsize, cd);
    printf("cd_tracks %d %d %d %d %d %d %d %d\n", ok, cd[0], cd[1], cd[2], cd[3], cd[4], cd[5], cd[6]);
    fflush(stdout);

    /* the heard-from-a-distance rule over a sweep (stdout is the dump compared with Python's, so the rest goes to stderr) */
    int max_rate = 0, max_vol = 0, max_pan = 0;
    long cases = 0;
    for(int id = 0; id < 8; id++) {
        const struct s2snd_sample *s = &smp[id];
        for(int r = 0; r <= 22; r++) {                                      /* revs / most: 0 .. 1.1 */
            const float pitch = (float)r * 0.05f * 2.0f;
            for(int di = 0; di <= 26; di++) {
                const float d = di < 25 ? (float)di * 2000.0f + 37.0f : di == 25 ? 49999.0f : 50000.0f;
                for(int li = -2; li <= 2; li++) {
                    const float last = d + (float)li * 450.0f + 11.0f;
                    for(int ai = -4; ai <= 4; ai++) {
                        const float across = (float)ai / 4.0f * (d < 50000.0f ? d : 50000.0f) + 3.0f;
                        struct s2snd_heard h;
                        int rr, rv, rp;
                        s2snd_heard(s, pitch, d, last, across, S2SND_RANGE, 1.0f, &h);
                        ref_heard(s, (double)pitch, d, last, across, 50000, 1.0, &rr, &rv, &rp);
                        const int er = abs((int)h.rate - rr), ev = abs((int)h.vol - rv), ep = abs(h.pan - rp);
                        if(er > max_rate) max_rate = er;
                        if(ev > max_vol) max_vol = ev;
                        if(ep > max_pan) max_pan = ep;
                        CHECK(er <= 2 && ev <= 1 && ep <= 1, "heard id %d pitch %g d %g last %g across %g: rate %u/%d vol %u/%d pan %d/%d", id,
                              (double)pitch, (double)d, (double)last, (double)across, h.rate, rr, h.vol, rv, h.pan, rp);
                        cases++;
                    }
                }
            }
        }
    }
    fprintf(stderr, "heard: %ld cases, largest difference to the double-precision reference: rate %d Hz, volume %d, pan %d\n", cases,
            max_rate, max_vol, max_pan);

    /* the player's engine on the real numbers: M_PORSCH at 9000 revs most, the camera 1300 behind (d = 1300) */
    {
        const struct s2snd_sample *s = &smp[0];
        const float revs[4] = { 2000.0f, 4500.0f, 7000.0f, 9000.0f };
        for(int k = 0; k < 4; k++) {
            struct s2snd_heard h;
            s2snd_heard(s, revs[k] / 9000.0f * 2.0f, 1300.0f, 1300.0f, 1300.0f * 0.0f + 1300.0f * 0.0f, S2SND_RANGE, 1.0f, &h);
            fprintf(stderr, "engine M_PORSCH (%u Hz, volume 0x%x) at %g of 9000 revs, 1300 away: %u Hz, volume 0x%x, pan 0x%x\n", s->rate, s->vol,
                    (double)revs[k], h.rate, h.vol, (unsigned)h.pan);
        }
    }

    /* the tyres */
    long tyre_cases = 0;
    for(int roll = 0; roll <= 6; roll++)
        for(int skid = 0; skid <= 4; skid++)
            for(int gi = 0; gi <= 12; gi++)
                for(int oi = 0; oi <= 1; oi++)
                    for(int si = -2; si <= 12; si++) {
                        const float grip = 0.75f + (float)gi * 0.02f, on = (float)oi * 0.75f, speed = (float)si * 6.5f;
                        uint32_t v = 0;
                        int rv = 0;
                        int a = s2snd_tyres(roll, skid, grip, on, speed, &v);
                        int b = ref_tyres(roll, skid, grip, on, speed < 0.0f ? -speed : speed, &rv);
                        CHECK(a == b && (a < 0 || abs((int)v - rv) <= 1), "tyres roll %d skid %d grip %g on %g speed %g: %d/%u vs %d/%d", roll, skid,
                              (double)grip, (double)on, (double)speed, a, v, b, rv);
                        tyre_cases++;
                    }
    fprintf(stderr, "tyres: %ld cases equal\n", tyre_cases);

    /* the knocks: 500 / 4000 / 10000 for a wall (bits 0x43fa0000, 0x457a0000, 0x461c4000), 5000 / 10000 for two cars */
    CHECK(s2snd_wall_sample(500.0f) == 0 && s2snd_wall_sample(500.5f) == 0xC && s2snd_wall_sample(4000.5f) == 0xD &&
          s2snd_wall_sample(10000.5f) == 0xE && s2snd_wall_sample(1.0e9f) == 0xE && s2snd_wall_sample(0.0f) == 0, "wall knock samples");
    CHECK(s2snd_car_sample(1.0f) == 0xF && s2snd_car_sample(5000.0f) == 0xF && s2snd_car_sample(5001.0f) == 0x10 &&
          s2snd_car_sample(10000.0f) == 0x10 && s2snd_car_sample(10001.0f) == 0x11, "car knock samples");
    CHECK(s2snd_engine_sample(2, 1) == 1 && s2snd_engine_sample(2, 4) == 6 && s2snd_engine_sample(0, 3) == 3, "engine samples");
    CHECK(s2snd_tunnel(7) && s2snd_tunnel(13) && s2snd_tunnel(34) && s2snd_tunnel(43) && !s2snd_tunnel(36) && !s2snd_tunnel(27) &&
          !s2snd_tunnel(8) && !s2snd_tunnel(0), "tunnel kinds");

    /* the registers: step is rate x 65536 / 48000 (a rate of 11025 Hz is 0.2297 of a sample a value), volume 63 at most
       (a full sample at the full volume, in the middle), the far side faded in a straight line */
    CHECK(s2snd_step(48000) == 65536 && s2snd_step(11025) == 15052 && s2snd_step(0) == 1 && s2snd_step(500000) == 262143, "step");
    const uint32_t full = (0x7FFFu * S2SND_FX_MASTER >> 15) * 0x7FFFu >> 22;      /* 63 at 0x2000 */
    CHECK(s2snd_vol_reg(0x7FFF, 0x8000) == (full | full << 8) && s2snd_vol_reg(0, 0x8000) == 0 && full > 40 && full < 128 &&
          s2snd_vol_reg(0x7FFF, 0) == full && s2snd_vol_reg(0x7FFF, 0xFFFF) == (full << 8) &&
          s2snd_vol_reg(0x7FFF, 0xA000) == ((((0x7FFFu * S2SND_FX_MASTER >> 15) * 0x5FFFu) >> 22) | full << 8), "volume register %08x %08x",
          s2snd_vol_reg(0x7FFF, 0xA000), s2snd_vol_reg(0x7FFF, 0));
    free(dat);
    free(game);
    if(bad) { fprintf(stderr, "%d checks failed\n", bad); return 1; }
    fprintf(stderr, "rules: all checks passed\n");
    return 0;
}
