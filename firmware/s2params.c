/*
 * Screamer 2's car and track numbers from the .DTR files (s2params.h)
 */
#include "s2params.h"
#include "dat.h"
#include <string.h>

static int is_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* the decimal text t[0 .. n) (a sign, digits, a point, digits) as the float nearest to it: an integer
   mantissa of at most 9 digits over (or times) a power of ten, both exact in a float up to 10^10 */
static float parse_float(const uint8_t *t, uint32_t n)
{
    uint32_t i = 0, m = 0;
    int neg = 0, digits = 0, frac = 0, seen_point = 0;
    if(i < n && (t[i] == '-' || t[i] == '+')) neg = t[i++] == '-';
    for(; i < n; i++) {
        if(t[i] == '.') { seen_point = 1; continue; }
        if(t[i] < '0' || t[i] > '9') break;
        if(digits < 9) {
            m = m * 10 + (uint32_t)(t[i] - '0');
            digits += m != 0;                       /* leading zeros are not digits */
            frac += seen_point;
        } else if(!seen_point) {                    /* more integer digits than fit: the value grows by ten */
            frac--;
        }
    }
    float v = (float)m;
    float p = 1.0f;
    for(int k = 0; k < (frac < 0 ? -frac : frac); k++) p *= 10.0f;
    v = frac >= 0 ? v / p : v * p;
    return neg ? -v : v;
}

static int32_t parse_int(const uint8_t *t, uint32_t n, int base)
{
    uint32_t i = 0, v = 0;
    int neg = 0;
    if(i < n && (t[i] == '-' || t[i] == '+')) neg = t[i++] == '-';
    for(; i < n; i++) {
        int d = t[i] >= '0' && t[i] <= '9' ? t[i] - '0' : t[i] >= 'a' && t[i] <= 'f' ? t[i] - 'a' + 10 :
                t[i] >= 'A' && t[i] <= 'F' ? t[i] - 'A' + 10 : 99;
        if(d >= base) break;
        v = v * (uint32_t)base + (uint32_t)d;
    }
    return (int32_t)(neg ? 0u - v : v);
}

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

int s2_dtr_words(const uint8_t *text, uint32_t size, const char *name, uint32_t *words, int max_words)
{
    uint32_t p = 0, nl = (uint32_t)strlen(name);
    int found = 0, n = 0;
    while(p < size) {
        uint32_t e = p;                             /* the line p .. e */
        while(e < size && text[e] != '\n') e++;
        uint32_t a = p, z = e;
        p = e < size ? e + 1 : e;
        if(!found) {                                /* the line that is the section's name */
            while(a < z && is_space(text[a])) a++;
            while(z > a && is_space(text[z - 1])) z--;
            found = z - a == nl && !memcmp(text + a, name, nl);
            continue;
        }
        while(a < z) {                              /* the words of a line of the section */
            while(a < z && is_space(text[a])) a++;
            if(a >= z || text[a] == '*') break;
            uint32_t w = a;
            while(a < z && !is_space(text[a])) a++;
            uint32_t len = a - w;
            if(len == 8 && !memcmp(text + w, "END_LIST", 8)) return n;
            if(len < 2 || n >= max_words) continue;
            switch(text[w]) {
            case 'd': words[n++] = (uint32_t)parse_int(text + w + 1, len - 1, 10); break;
            case 'x': words[n++] = (uint32_t)parse_int(text + w + 1, len - 1, 16); break;
            case 'f': words[n++] = float_bits(parse_float(text + w + 1, len - 1)); break;
            default: break;
            }
        }
    }
    return found ? n : 0;
}

int s2_params_cars(const uint8_t *text, uint32_t size, struct s2_car_params *out)
{
    _Static_assert(sizeof(struct s2_car_params) == 0x58, "CAR_PARAMS row");
    uint32_t w[S2_CARS_TOTAL * 22];
    if(s2_dtr_words(text, size, "CAR_PARAMS", w, S2_CARS_TOTAL * 22) != S2_CARS_TOTAL * 22) return 0;
    memcpy(out, w, sizeof w);
    return 1;
}

int s2_params_track(const uint8_t *text, uint32_t size, struct s2_track_params *out)
{
    _Static_assert(sizeof(struct s2_track_params) == 0x110, "TRACK_DATA");
    uint32_t w[68];
    if(s2_dtr_words(text, size, "TRACK_DATA", w, 68) != 68) return 0;
    memcpy(out, w, sizeof w);
    return 1;
}

static uint32_t entry_text(const uint8_t *dat, uint32_t size, const char *name, uint8_t *scratch, uint32_t cap)
{
    struct dat_entry e;
    if(!dat_find(dat, size, name, &e)) return 0;
    return dat_extract(dat, size, &e, scratch, cap);
}

int s2_params_cars_dat(const uint8_t *game_dat, uint32_t size, uint8_t *scratch, uint32_t cap, struct s2_car_params *out)
{
    uint32_t n = entry_text(game_dat, size, "CARS.DTR", scratch, cap);
    return n && s2_params_cars(scratch, n, out);
}

int s2_params_track_dat(const uint8_t *map_dat, uint32_t size, int track, uint8_t *scratch, uint32_t cap,
                        struct s2_track_params *out)
{
    char name[16] = "MAP0DAT.DTR";
    name[3] = (char)('0' + track);
    uint32_t n = entry_text(map_dat, size, name, scratch, cap);
    return n && s2_params_track(scratch, n, out);
}

int s2_dtr_names(const uint8_t *text, uint32_t size, const char *section, char names[][12], int max)
{
    uint32_t p = 0, sl = (uint32_t)strlen(section);
    int n = 0, in = 0;
    while(p < size) {
        uint32_t e = p, a;
        while(e < size && text[e] != '\n') e++;
        a = p;
        p = e < size ? e + 1 : e;
        while(a < e && is_space(text[a])) a++;
        if(!in) {
            uint32_t z = e;
            while(z > a && is_space(text[z - 1])) z--;
            in = z - a == sl && !memcmp(text + a, section, sl);
            continue;
        }
        while(a < e) {                              /* the words of the line */
            while(a < e && is_space(text[a])) a++;
            if(a >= e || text[a] == '*') break;
            uint32_t w = a;
            while(a < e && !is_space(text[a])) a++;
            if(a - w == 8 && !memcmp(text + w, "END_LIST", 8)) return n;
            if(text[w] == 's' && a - w >= 2 && n < max) {
                uint32_t len = a - w - 1;
                if(len > 11) len = 11;
                memcpy(names[n], text + w + 1, len);
                names[n++][len] = 0;
            }
        }
    }
    return n;
}
