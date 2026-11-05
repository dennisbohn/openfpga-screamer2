/*
 * firmware/smg.c against the cases tools/s2/check_smg_s2.py made with tools/s2/smg_ref.py (see there for the file): the rows of the table (the seven tracks'
 * palettes and colours with every texel, random palettes), the textures' copies and the rows' plan (made-up libraries). Run by tools/s2/check_smg_s2.sh.
 * Also: the originals' pixels are never touched (only the material records change), and a deliberately damaged expectation is found (the negative control).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smg.h"

static uint32_t rd(FILE *f)
{
    uint8_t b[4];
    if(fread(b, 1, 4, f) != 4) { fprintf(stderr, "short file\n"); exit(2); }
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void rdb(FILE *f, void *p, size_t n)
{
    if(n && fread(p, 1, n, f) != n) { fprintf(stderr, "short file\n"); exit(2); }
}

int main(int argc, char **argv)
{
    if(argc < 2) { fprintf(stderr, "usage: check_smg_s2 <cases>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if(!f) { perror(argv[1]); return 2; }
    const uint32_t cases = rd(f);
    int bad = 0, tables = 0, rows = 0, libs = 0, merged = 0, controls = 0;
    for(uint32_t c = 0; c < cases; c++) {
        const uint32_t kind = rd(f);
        if(kind == 1) {
            uint8_t col[768];
            int32_t ast[3], a2[3];
            rdb(f, col, 768);
            for(int i = 0; i < 3; i++) ast[i] = (int32_t)rd(f);
            for(int i = 0; i < 3; i++) a2[i] = (int32_t)rd(f);
            const uint32_t n = rd(f);
            uint8_t texel[112];
            rdb(f, texel, n);
            static uint8_t want[112][256], got[SMG_TABLE];
            for(uint32_t i = 0; i < n; i++) rdb(f, want[i], 256);
            struct smg s = { .rows = (int)n, .ramps = 31 };
            for(uint32_t i = 0; i < n; i++) s.alive[i] = texel[i];
            smg_table(&s, col, ast, a2, got);
            for(uint32_t i = 0; i < n; i++) {
                rows++;
                if(memcmp(got + 256 * (i + 1), want[i], 256)) {
                    bad++;
                    printf("case %u: row of texel %d differs\n", c, texel[i]);
                }
            }
            tables++;
            continue;
        }
        const uint32_t ramps = rd(f), nfront = rd(f), ntrack = rd(f), size = rd(f);
        static uint8_t front[16 * 8], mlb[1 << 24], want_mlb[1 << 24], want_front[16 * 8], keep[1 << 24];
        rdb(f, front, 16 * nfront);
        rdb(f, mlb, size);
        memcpy(keep, mlb, size);
        const uint32_t newsize = rd(f);
        rdb(f, want_front, 16 * nfront);
        rdb(f, want_mlb, newsize);
        const uint32_t nalive = rd(f);
        uint8_t alive[62], lut[256];
        rdb(f, alive, 62);
        rdb(f, lut, 256);
        const uint32_t found = rd(f);
        struct smg s;
        const uint32_t got = smg_copies(&s, (int)ramps, front, nfront, mlb, ntrack, size, sizeof mlb);
        libs++;
        int ok = got == newsize && !memcmp(front, want_front, 16 * nfront) && !memcmp(mlb, want_mlb, newsize) && s.rows == (int)nalive &&
                 !memcmp(s.alive, alive, nalive) && !memcmp(s.lut, lut, 256) && s.found == (int)found;
        if(!ok) {
            bad++;
            printf("case %u: the copies differ (size %u, wanted %u)\\n", c, got, newsize);
        }
        /* the originals' pixels: everything outside the track's record table is as it was, up to the old end (the copies lie after it) */
        if(memcmp(mlb + 16 * ntrack, keep + 16 * ntrack, size - 16 * ntrack)) {
            bad++;
            printf("case %u: an original texture was changed\n", c);
        }
        if(nalive == SMG_FREE) merged++;
        /* the negative control: one byte of the wanted copy damaged must be found by the same comparison */
        if(newsize > size) {
            want_mlb[size + (c * 37u) % (newsize - size)] ^= 0x5A;
            if(!memcmp(mlb, want_mlb, newsize)) {
                bad++;
                printf("case %u: the negative control was not found\n", c);
            }
            controls++;
        }
    }
    printf("%u cases: %d row tables (%d rows), %d libraries (%d with all %d rows used), %d negative controls; %d wrong\n", cases, tables, rows, libs, merged, SMG_FREE, controls, bad);
    printf(bad ? "FAIL\n" : "PASS\n");
    return bad != 0;
}
