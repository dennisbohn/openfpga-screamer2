/*
 * Screamer 2's sound: the samples and the game's rules for them (s2snd.h)
 */
#include "s2snd.h"
#include "s2params.h"
#include "dat.h"
#include <math.h>
#include <string.h>

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* the next word of the text at *p: spaces and lines that begin with `*` (comments) are skipped; its length, 0 at the end */
static uint32_t word(const char **p, const char *end, const char **w)
{
    const char *s = *p;
    for(;;) {
        while(s < end && is_space(*s)) s++;
        if(s < end && *s == '*') {
            while(s < end && *s != '\n') s++;
            continue;
        }
        break;
    }
    *w = s;
    while(s < end && !is_space(*s)) s++;
    *p = s;
    return (uint32_t)(s - *w);
}

/* a field: d and decimal digits, or x and hexadecimal ones, maybe with a minus (x-1) */
static int32_t field(const char *t, uint32_t n, int *ok)
{
    uint32_t i = 1, v = 0, base = 10;
    int neg = 0;
    *ok = 0;
    if(n < 2 || (t[0] != 'd' && t[0] != 'x')) return 0;
    if(t[0] == 'x') base = 16;
    if(t[i] == '-') {
        neg = 1;
        i++;
    }
    for(; i < n; i++) {
        uint32_t d = t[i] >= '0' && t[i] <= '9' ? (uint32_t)(t[i] - '0') : (t[i] | 0x20) >= 'a' && (t[i] | 0x20) <= 'f' ? (uint32_t)((t[i] | 0x20) - 'a' + 10) : 99;
        if(d >= base) return 0;
        v = v * base + d;
    }
    *ok = i > 1u + (uint32_t)neg;
    return (int32_t)(neg ? 0u - v : v);
}

/* the sample table `table` (GAME.DTR, MENU.DTR, AMBIENT<n>.DTR) of the container into out[count]: the sample numbered first + k is out[k] */
static int parse_table(const uint8_t *dat, uint32_t size, uint32_t base, const char *table, struct s2snd_sample *out, int count, int32_t first)
{
    static uint8_t text[8192];
    struct dat_entry e;
    int found = 0;
    memset(out, 0, sizeof *out * (size_t)count);
    if(!dat_find(dat, size, table, &e) || e.size >= sizeof text) return 0;
    uint32_t n = dat_extract(dat, size, &e, text, sizeof text);
    const char *p = (const char *)text, *end = p + n, *w;
    uint32_t len;
    while((len = word(&p, end, &w)) != 0 && !(len == 10 && !memcmp(w, "SOUND_DATA", 10)))
        ;
    while(len && (len = word(&p, end, &w)) != 0) {
        if(len == 8 && !memcmp(w, "END_LIST", 8)) break;
        if(w[0] != 's' || len < 2 || len > 16) continue;
        char name[17];
        int32_t f[8];
        int ok = 1, k;
        memcpy(name, w + 1, len - 1);
        name[len - 1] = 0;
        for(k = 0; k < 8 && ok; k++) {
            const char *t;
            uint32_t m = word(&p, end, &t);
            f[k] = m ? field(t, m, &ok) : (ok = 0);
        }
        if(!ok || f[0] < first || f[0] - first >= count) continue;
        /* id, loop length, loop count, volume; rate, bits, channels, format */
        struct s2snd_sample *s = &out[f[0] - first];
        if(f[5] != 8 || f[6] != 0 || f[7] != 0) continue;           /* 8-bit mono unsigned only */
        if(!dat_find(dat, size, name, &e) || e.method != 0 || e.size == 0 || e.offset > size || e.size > size - e.offset) continue;
        s->addr = base + e.offset;
        s->len = e.size;
        s->rate = f[4] > 0 ? (uint32_t)f[4] : 11025u;
        s->vol = (uint32_t)f[3] & 0x7FFF;
        s->loop = f[2] != 0;                                        /* the loop length (f[1]) is 0 in all of them: the whole sample */
        found++;
    }
    return found;
}

int s2snd_parse(const uint8_t *dat, uint32_t size, uint32_t base, struct s2snd_sample *out)
{
    return parse_table(dat, size, base, "GAME.DTR", out, S2SND_SAMPLES, 0);
}

int s2snd_parse_menu(const uint8_t *dat, uint32_t size, uint32_t base, struct s2snd_sample *out)
{
    return parse_table(dat, size, base, "MENU.DTR", out, S2SND_MENU_SAMPLES, 0);
}

int s2snd_parse_ambient(const uint8_t *dat, uint32_t size, uint32_t base, int track, struct s2snd_sample *out)
{
    char table[16] = "AMBIENT0.DTR";
    table[7] = (char)('0' + track - 1);
    return parse_table(dat, size, base, table, out, S2SND_AMBIENT, S2SND_AMBIENT_FIRST);
}

int s2snd_cd_tracks(const uint8_t *game_dat, uint32_t size, int out[7])
{
    static uint8_t text[2048];
    struct dat_entry e;
    uint32_t w[7];
    if(!dat_find(game_dat, size, "MISC.DTR", &e) || e.size > sizeof text) return 0;
    uint32_t n = dat_extract(game_dat, size, &e, text, sizeof text);
    if(!n || s2_dtr_words(text, n, "CD_TRACKS", w, 7) != 7) return 0;
    for(int i = 0; i < 7; i++) out[i] = (int)w[i];
    return 1;
}

uint32_t s2snd_step(uint32_t rate)
{
    float s = (float)rate * (65536.0f / 48000.0f);
    return s < 1.0f ? 1u : s > 262143.0f ? 262143u : (uint32_t)s;
}

uint32_t s2snd_vol_reg(uint32_t vol, int32_t pan)
{
    uint32_t t = (vol > 0x7FFF ? 0x7FFFu : vol) * S2SND_FX_MASTER >> 15;
    uint32_t fl = pan <= 0x8000 ? 0x7FFFu : (uint32_t)(0xFFFF - (pan > 0xFFFF ? 0xFFFF : pan));
    uint32_t fr = pan >= 0x8000 ? 0x7FFFu : (uint32_t)(pan < 0 ? 0 : pan);
    return (t * fl >> 22) | (t * fr >> 22) << 8;
}

int s2snd_engine_sample(int team, int model)
{
    return model == 4 ? team + 4 : model;
}

int s2snd_tunnel(int kind)
{
    return kind == 7 || kind == 13 || kind == 0x22 || kind == 0x2B;
}

void s2snd_heard(const struct s2snd_sample *s, float pitch, float d, float last_d, float across, float range,
                 float doppler, struct s2snd_heard *out)
{
    if(!(range > d)) {
        out->rate = 11025;
        out->vol = 0;
        out->pan = 0x8000;
        return;
    }
    float own = (float)(int32_t)((float)s->rate * (pitch * pitch));
    float rate = own + own * ((last_d - d) / range) * 5.0f * doppler;
    float nr = (range - d) / range;
    out->rate = rate > 0.0f ? (uint32_t)rate : 0;
    out->vol = (uint32_t)((float)s->vol * (nr * nr));
    out->pan = (int32_t)(32768.0f - cosf((1.0f - across / range) * 1.87f) * 32767.0f);
}

int s2snd_tyres(int roll, int skid, float grip, float on_ground, float speed, uint32_t *vol)
{
    int squeal = 0;
    if(on_ground > 0.0f && !(grip >= 0.93f)) {
        float q = (0.93f - grip) / 0.1f;
        squeal = (int)((q <= 1.0f ? q : 1.0f) * 29952.0f);
    }
    if(squeal == 0 || skid == 0) {
        if(roll == 0) return -1;
        float v = speed < 0.0f ? -speed : speed;                    /* (the game's: negative when reversing, a loudness it never meant) */
        v = v < 50.0f ? v : 50.0f;
        *vol = v * 655.0f >= 32767.0f ? 32767u : (uint32_t)(v * 655.0f);
        return S2SND_GROUND + roll;
    }
    *vol = (uint32_t)squeal;
    return 7 + skid;
}

int s2snd_wall_sample(float hard)
{
    return hard > 10000.0f ? S2SND_HIT + 2 : hard > 4000.0f ? S2SND_HIT + 1 : hard > 500.0f ? S2SND_HIT : 0;
}

int s2snd_car_sample(float knock)
{
    int k = (int)knock;
    return k > 0x2710 ? S2SND_CRASH + 2 : k > 0x1388 ? S2SND_CRASH + 1 : S2SND_CRASH;
}
