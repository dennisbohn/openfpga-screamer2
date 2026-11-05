/*
 * The cars' windows' reflection picture (s2env.h)
 */
#include "s2env.h"
#include <string.h>

static uint32_t ld16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t ld32(const uint8_t *p) { return ld16(p) | ld16(p + 2) << 16; }

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int s2_mlb_texture(int32_t ro, uint32_t size)
{
    return ro > 0 && (uint32_t)ro + 12 <= size;
}

/* the loop of the PC port's mlb_read */
uint32_t s2_mlb_records(const uint8_t *m, uint32_t size)
{
    uint32_t end = size, k = 0;
    while((k + 1) * 16 <= end) {
        int32_t ro = (int32_t)ld32(m + 16 * k + 4);
        if(ro > 0 && (uint32_t)ro + 12 <= size && (uint32_t)ro < end) end = (uint32_t)ro;
        k++;
    }
    return k;
}

int s2_env_window(const uint8_t *rec)
{
    return ld32(rec) == 160;
}

uint32_t s2_env_frames(const uint8_t *m, uint32_t size)
{
    if(size < 16 || ld32(m) != 160 || ld32(m + 8) != 128 || ld32(m + 12) != 128) return 0;
    int32_t ro = (int32_t)ld32(m + 4);
    if(!s2_mlb_texture(ro, size)) return 0;
    int32_t cnt = (int32_t)ld32(m + ro);
    uint32_t n = cnt > 0 ? (uint32_t)cnt & 0xFFFF : 1;
    if(n < 1 || n > S2_ENV_MAX || (uint64_t)ro + 8 + 4 * (uint64_t)n > size) return 0;
    for(uint32_t fr = 0; fr < n; fr++) {
        uint32_t o = ld32(m + ro + 8 + 4 * fr);
        if((uint64_t)o + 127 * 256 + 128 > size) return 0;
    }
    return n;
}

void s2_env_write(uint8_t *dst, const uint8_t *m, uint32_t n, uint32_t at)
{
    int32_t ro = (int32_t)ld32(m + 4);
    memset(dst, 0, S2_ENV_HEAD);
    wr32(dst, n);
    wr32(dst + 4, ld32(m + ro + 4));
    for(uint32_t fr = 0; fr < n; fr++) {
        wr32(dst + 8 + 4 * fr, at + S2_ENV_HEAD + S2_ENV_FRAME * fr);
        const uint8_t *src = m + ld32(m + ro + 8 + 4 * fr);
        uint8_t *to = dst + S2_ENV_HEAD + S2_ENV_FRAME * fr;
        for(uint32_t r = 0; r < 256; r++, to += 256) {
            memcpy(to, src + 256 * (r & 127), 128);
            memcpy(to + 128, src + 256 * (r & 127), 128);
        }
    }
}

void s2_env_windows(uint8_t *mlb, uint32_t size, uint32_t env_at)
{
    const uint32_t nrec = s2_mlb_records(mlb, size);
    for(uint32_t k = 0; k < nrec; k++)
        if(s2_env_window(mlb + 16 * k) && s2_mlb_texture((int32_t)ld32(mlb + 16 * k + 4), size)) wr32(mlb + 16 * k + 4, env_at);
}
