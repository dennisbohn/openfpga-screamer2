/*
 * The track image from the game's containers (track_build.h), as tools/make_track.py main()
 * assembles it (track_build(), Screamer 1: no longer reached) and tools/s2/make_track_s2.py
 * (track_build_s2(), Screamer 2). The sections go straight into out one after the other; each
 * section's offset and count wait in offs[] and counts[] for the header, which is written last.
 */
#include "track_build.h"
#include "dat.h"
#include "tb_mmp.h"
#include "tb_spl.h"
#include "tb_gfx.h"
#include "tb_hud.h"
#include "crc32.h"
#include <string.h>

int track_build_difficulty = 1;         /* NORMAL, as make_track.py */

#define FIRST       0x100       /* the first section, after the header */
#define BLOCK       0x40000     /* bytes per CRC block */
#define MAX_BLOCKS  28          /* the CRCs that fit into the header (0x90 .. 0x100) */
#define SECTIONS    16
#define MODELS      7           /* AUTO00.3DX .. AUTO06.3DX */

/* The header's block CRC-32s are for the native check (byte-identical with tools/make_track*.py); on the Pocket
   nothing reads them and they take over a second, so there the words stay 0 */
#ifdef __riscv
#define BLOCK_CRC(p, e) ((void)(p), (void)(e), 0u)
#else
#define BLOCK_CRC(p, e) crc32(p, e)
#endif

#ifdef __riscv
static inline uint32_t now(void)
{
    uint32_t c;
    __asm__ volatile ("csrr %0, cycle" : "=r"(c));
    return c;
}
#else
static inline uint32_t now(void) { return 0; }
#endif

struct build {
    uint8_t *out, *scratch;
    uint32_t cap, pos, scratch_cap, used;
    int sections;                           /* written so far */
    uint32_t offs[SECTIONS], counts[SECTIONS];
    char track[8];                          /* "MAPn" */
    struct track_build_info *info;
};

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* what stopped the build, for the log; returns 0 */
static int fail(struct build *b, const char *what)
{
    size_t n = strlen(what);
    if(n > sizeof b->info->fail - 1) n = sizeof b->info->fail - 1;
    memcpy(b->info->fail, what, n);
    b->info->fail[n] = 0;
    return 0;
}

/* the track's name with an ending: "MAP1" + ".MMP" */
static const char *name(struct build *b, const char *end, char buf[16])
{
    size_t n = strlen(b->track);
    memcpy(buf, b->track, n);
    strcpy(buf + n, end);
    return buf;
}

/* entry e of the container (file, size) into to (room bytes): 1, or 0 if it does not fit or
   is malformed (an empty entry as well) */
static int extract(struct build *b, const uint8_t *file, uint32_t size, const struct dat_entry *e,
                   uint8_t *to, uint32_t room)
{
    uint32_t t0 = now();
    int ok = e->size > 0 && dat_extract(file, size, e, to, room) == e->size;
    b->info->unpack_cycles += now() - t0;
    return ok || fail(b, e->name);
}

/* the entry called what into the scratch: its start, *size its bytes; 0 if missing or malformed */
static const uint8_t *unpack(struct build *b, const uint8_t *file, uint32_t file_size, const char *what,
                             uint32_t *size)
{
    struct dat_entry e;
    uint8_t *p = b->scratch + b->used;
    uint32_t room = b->scratch_cap - b->used;
    if(!dat_find(file, file_size, what, &e)) {
        fail(b, what);
        return 0;
    }
    if(!extract(b, file, file_size, &e, p, room)) return 0;
    b->used += e.size + 3 > room ? room : (e.size + 3) & ~3u;   /* the next one word-aligned */
    *size = e.size;
    return p;
}

/* where the next section goes, and its room */
static uint8_t *at(const struct build *b) { return b->out + b->pos; }
static uint32_t room(const struct build *b) { return b->cap - b->pos; }

/* the next section is n bytes (0: it could not be made) with count for the header; the gap to
   the next 256-byte boundary zeroed */
static int section(struct build *b, uint32_t n, uint32_t count, const char *what)
{
    if(n == 0 || n > room(b)) return fail(b, what);
    uint32_t end = b->pos + n, next = (end + 255) & ~255u;
    if(next < end || next > b->cap) return fail(b, what);
    memset(b->out + end, 0, next - end);
    b->offs[b->sections] = b->pos;
    b->counts[b->sections++] = count;
    b->pos = next;
    return 1;
}

/* an entry of the graphics container unchanged as the next section (MLB, UNR) */
static int raw(struct build *b, const struct track_files *f, const char *end)
{
    char buf[16];
    struct dat_entry e;
    if(!dat_find(f->gfx, f->gfx_size, name(b, end, buf), &e)) return fail(b, buf);
    return extract(b, f->gfx, f->gfx_size, &e, at(b), room(b)) && section(b, e.size, e.size, buf);
}

/* the car models: each .3DX unpacked straight to its place in the section */
static int models(struct build *b, const struct track_files *f)
{
    struct dat_entry e[MODELS];
    uint32_t size[MODELS];
    const uint8_t *x3d[MODELS];
    for(int k = 0; k < MODELS; k++) {
        char buf[16] = "AUTO00.3DX";
        buf[5] = (char)('0' + k);
        if(!dat_find(f->gfx, f->gfx_size, buf, &e[k])) return fail(b, buf);
        size[k] = e[k].size;
    }
    for(int k = 0; k < MODELS; k++) {
        uint32_t o = tb_car_models_at(size, MODELS, k);
        if(o == 0 || o > room(b) || !extract(b, f->gfx, f->gfx_size, &e[k], at(b) + o, room(b) - o)) return 0;
        x3d[k] = at(b) + o;
    }
    return section(b, tb_car_models(x3d, size, MODELS, at(b), room(b)), MODELS, "car models");
}

uint32_t track_build(const struct track_files *f, int n, uint8_t *out, uint32_t cap,
                     uint8_t *scratch, uint32_t scratch_cap, struct track_build_info *info)
{
    uint32_t t0 = now();
    struct build b = { .out = out, .scratch = scratch, .cap = cap, .pos = FIRST, .scratch_cap = scratch_cap,
                       .track = "MAP0", .info = info };
    info->fail[0] = 0;
    info->cycles = info->unpack_cycles = 0;
    if(n < 1 || n > 9 || cap < FIRST) return (uint32_t)fail(&b, "track number");
    b.track[3] = (char)('0' + n);
    memset(out, 0, FIRST);

    char buf[16];
    uint32_t col_n = 0, mmp_n = 0, spl_n = 0, mrk_n = 0, cars_n = 0, spr_n = 0, fnt_n = 0, let_n = 0, res_n = 0,
             txt_n = 0, bck_n = 0, dtr_n = 0, len, count = 0;
    const uint8_t *col, *mmp, *spl, *mrk, *cars, *spr, *fnt, *let, *res, *txt, *bck, *dtr;
    uint8_t td[12];                         /* TRACK_DATA: bonus_time, camera_hi, colour */
    if(!raw(&b, f, ".MLB") || !raw(&b, f, ".UNR")) return 0;
    if(!(col = unpack(&b, f->gfx, f->gfx_size, name(&b, ".COL", buf), &col_n)) ||
       !section(&b, tb_palette(col, col_n, at(&b), room(&b)), 0, "palette")) return 0;
    if(!(mmp = unpack(&b, f->gfx, f->gfx_size, name(&b, ".MMP", buf), &mmp_n))) return 0;
    len = tb_mmp_placements(mmp, mmp_n, at(&b), room(&b), &count);
    if(!section(&b, len, count, "placements")) return 0;
    if(!(spl = unpack(&b, f->data, f->data_size, name(&b, "OPP.SPL", buf), &spl_n))) return 0;
    len = tb_path(spl, spl_n, at(&b), room(&b));
    if(!section(&b, len, len / 12, "camera path")) return 0;
    len = tb_mmp_ground(mmp, mmp_n, at(&b), room(&b), &count);
    if(!section(&b, len, count, "ground index")) return 0;
    if(!(mrk = unpack(&b, f->data, f->data_size, name(&b, ".MRK", buf), &mrk_n))) return 0;
    len = tb_markers(mrk, mrk_n, at(&b), room(&b));
    if(!section(&b, len, len / 32, "markers")) return 0;
    if(!(cars = unpack(&b, f->gen, f->gen_size, "CARS.DTR", &cars_n))) return 0;
    len = tb_cars(cars, cars_n, at(&b), room(&b));
    if(!section(&b, len, len / 68, "cars") || !models(&b, f)) return 0;
    len = tb_mmp_dirs(mmp, mmp_n, at(&b), room(&b), &count);
    if(!section(&b, len, count, "road points")) return 0;
    len = tb_mmp_walls(mmp, mmp_n, at(&b), room(&b), &count);
    if(!section(&b, len, count, "walls")) return 0;
    if(!(spr = unpack(&b, f->game, f->game_size, "GAMEL.SPR", &spr_n)) ||
       !(fnt = unpack(&b, f->game, f->game_size, "GAMEL.FNT", &fnt_n)) ||
       !(let = unpack(&b, f->game, f->game_size, "IMPLET_L.PKC", &let_n)) ||
       !(res = unpack(&b, f->game, f->game_size, "IMPRES_L.PKC", &res_n)) ||
       !(txt = unpack(&b, f->local, f->local_size, "GAME.TXT", &txt_n))) return 0;
    len = tb_hud_build(spr, spr_n, fnt, fnt_n, let, let_n, res, res_n, txt, txt_n, at(&b), room(&b));
    if(!section(&b, len, len, "race screen")) return 0;
    if(!(bck = unpack(&b, f->gfx, f->gfx_size, name(&b, ".BCK", buf), &bck_n)) ||
       !section(&b, tb_backdrop(bck, bck_n, at(&b), room(&b)), 0, "backdrop")) return 0;
    char dtr_end[8] = "A.DTR";                  /* MAPn + 'A' + the difficulty + .DTR (0x13251) */
    dtr_end[0] = (char)('A' + track_build_difficulty);
    if(!(dtr = unpack(&b, f->data, f->data_size, name(&b, dtr_end, buf), &dtr_n))) return 0;
    len = tb_opponents(spl, spl_n, dtr, dtr_n, at(&b), room(&b));
    if(!section(&b, len, len, "opponents")) return 0;
    if(tb_grid_pose(spl, spl_n, mmp, mmp_n, out + 0x5C, 12) != 12) return (uint32_t)fail(&b, "grid pose");
    if(tb_track_data(dtr, dtr_n, td, sizeof td) != sizeof td) return (uint32_t)fail(&b, "TRACK_DATA");

    /* the header: offsets and counts as make_track.py packs them, then the block CRCs */
    uint32_t size = b.pos, blocks = (size - FIRST + BLOCK - 1) / BLOCK;
    if(blocks > MAX_BLOCKS) return (uint32_t)fail(&b, "image size");
    memcpy(out, "SCRK", 4);
    wr32(out + 0x04, 6);
    wr32(out + 0x08, b.offs[0]);
    wr32(out + 0x0C, b.counts[0]);
    wr32(out + 0x10, b.offs[1]);
    wr32(out + 0x14, b.counts[1]);
    wr32(out + 0x18, b.offs[2]);
    for(int k = 3; k <= 10; k++) {
        wr32(out + 0x1C + 8 * (k - 3), b.offs[k]);
        wr32(out + 0x20 + 8 * (k - 3), b.counts[k]);
    }
    wr32(out + 0x68, b.offs[11]);
    wr32(out + 0x6C, b.counts[11]);
    memcpy(out + 0x70, td, 8);              /* bonus_time, camera_hi */
    wr32(out + 0x78, b.offs[12]);
    memcpy(out + 0x7C, td + 8, 4);          /* the ground colour */
    wr32(out + 0x80, b.offs[13]);
    wr32(out + 0x84, b.counts[13]);
    wr32(out + 0x88, size);
    wr32(out + 0x8C, blocks);
    for(uint32_t k = 0; k < blocks; k++) {
        uint32_t o = FIRST + k * BLOCK, e = size - o < BLOCK ? size : o + BLOCK;
        wr32(out + 0x90 + 4 * k, BLOCK_CRC(out + o, out + e));
    }
    info->cycles = now() - t0;
    return size;
}

/*
 * Screamer 2 (tools/s2/make_track_s2.py is the reference; docs/screamer2/comparison.md). The image is
 * version 7: the car materials in front of the MLB, then the sections of version 6 with the camera path,
 * the markers, the race screen and the opponents empty, and the cars of the grid (four, each its own team's:
 * s2teams.h) merged in:
 *   - the geometry unit reads a face's material at a signed 16-bit offset from the MLB's start, so the
 *     cars' material records (16 bytes each) are collected in a section in front of the track's MLB and the
 *     cars' faces point there (a negative offset); their texture records and pixels stay in the cars' own
 *     .MLB files, appended after the track's MLB, with the offsets inside them (the records' texture
 *     record, a texture record's frames) moved by where the file lies
 *   - the cars' windows (routine 160) are drawn by the game with the first material of ENV.MLB (GFX/ENVPn01.DAT),
 *     a 128 x 128 picture of 1 to 3 frames whose texel address is masked with 0x7f7f; the hardware wraps routine
 *     160's u, v at 256, so every frame is tiled 2 x 2 into 256 x 256 after the cars' libraries (still inside the
 *     MLB section) under a texture record of its own, and the cars' routine-160 material records point to it.
 *     The image stays version 7 (no new section)
 */
#define S2_ENV_HEAD  256u               /* the reflection's texture record, padded to this */
#define S2_ENV_FRAME 65536u             /* a tiled frame: 256 rows of 256 */
#define S2_ENV_MAX   8u

static uint32_t ld16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t ld32(const uint8_t *p) { return ld16(p) | ld16(p + 2) << 16; }
static uint32_t pad256(uint32_t n) { return (n + 255) & ~255u; }

/* The material records of an .MLB: the table of 16-byte records runs up to the first texture record
   (the loop of the PC port's mlb_read) */
static uint32_t mlb_records(const uint8_t *m, uint32_t size)
{
    uint32_t end = size, k = 0;
    while((k + 1) * 16 <= end) {
        int32_t ro = (int32_t)ld32(m + 16 * k + 4);
        if(ro > 0 && (uint32_t)ro + 12 <= size && (uint32_t)ro < end) end = (uint32_t)ro;
        k++;
    }
    return k;
}

static int valid_record(int32_t ro, uint32_t size) { return ro > 0 && (uint32_t)ro + 12 <= size; }

/* a car's material records (nrec) into recs, each one's texture record moved to where the file lies (at); a
   routine-160 material (a window) gets the reflection's texture record, at env_at (0: no reflection) */
static void lib_records(const uint8_t *m, uint32_t size, uint32_t nrec, uint32_t at, uint32_t env_at, uint8_t *recs)
{
    for(uint32_t k = 0; k < nrec; k++) {
        memcpy(recs + 16 * k, m + 16 * k, 16);
        int32_t ro = (int32_t)ld32(m + 16 * k + 4);
        if(valid_record(ro, size)) wr32(recs + 16 * k + 4, env_at && ld32(m + 16 * k) == 160 ? env_at : (uint32_t)ro + at);
    }
}

/* ENV.MLB (size bytes): its first material is routine 160, 128 x 128; its texture record is n (1 to 3 frames), a
   word, the frames' offsets; a frame is 128 rows of 256 bytes with the picture in the first 128 columns. Returns
   n, 0 if it is not that */
static uint32_t env_frames(const uint8_t *m, uint32_t size)
{
    if(size < 16 || ld32(m) != 160 || ld32(m + 8) != 128 || ld32(m + 12) != 128) return 0;
    int32_t ro = (int32_t)ld32(m + 4);
    if(!valid_record(ro, size)) return 0;
    int32_t cnt = (int32_t)ld32(m + ro);
    uint32_t n = cnt > 0 ? (uint32_t)cnt & 0xFFFF : 1;
    if(n < 1 || n > S2_ENV_MAX || (uint64_t)ro + 8 + 4 * (uint64_t)n > size) return 0;
    for(uint32_t fr = 0; fr < n; fr++) {
        uint32_t o = ld32(m + ro + 8 + 4 * fr);
        if((uint64_t)o + 127 * 256 + 128 > size) return 0;
    }
    return n;
}

/* the reflection's block of ENV.MLB (n frames, checked) at dst, to lie at offset at of the merged MLB: the texture
   record (n, the ENV record's word +4, the frames' offsets) padded to S2_ENV_HEAD, then each frame tiled 2 x 2
   into 256 x 256 (the game masks a texel address with 0x7f7f, the hardware u, v with 0xff, and a window's u, v are
   whole texels: STARTL.EXE 0xcd771, 0xcd118) */
static void env_write(uint8_t *dst, const uint8_t *m, uint32_t n, uint32_t at)
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

/* a car's .MLB copied to m (size bytes) to lie at offset at: the frames' offsets of every texture record (n,
   one word, then n offsets; one if n < 1) moved by at; two materials may share a record, which is moved once */
static void lib_relocate(uint8_t *m, uint32_t size, uint32_t nrec, uint32_t at)
{
    for(uint32_t k = 0; k < nrec; k++) {
        int32_t ro = (int32_t)ld32(m + 16 * k + 4);
        if(!valid_record(ro, size)) continue;
        int again = 0;
        for(uint32_t j = 0; j < k && !again; j++) again = (int32_t)ld32(m + 16 * j + 4) == ro;
        if(again) continue;
        int32_t cnt = (int32_t)ld32(m + ro);
        uint32_t frames = cnt > 0 ? (uint32_t)cnt & 0xFFFF : 1;
        for(uint32_t fr = 0; fr < frames; fr++) {
            uint64_t p = (uint64_t)ro + 8 + 4 * (uint64_t)fr;
            if(p + 4 > size) break;
            wr32(m + p, ld32(m + p) + at);
        }
    }
}

/* a .3DX car's faces onto the car materials: every face's material offset (face +4, an offset in the car's own
   table) moved by delta, negative. Every block of the header's table at the three levels of detail (entry
   b + 13 L), every face of its 16 rows; moved only while it is not yet negative, so a face in several rows is
   moved once. 1, or 0 if the file is malformed. */
static int faces_onto(uint8_t *x, uint32_t size, int32_t delta)
{
    if(size < 0x18) return 0;
    int32_t nb = (int32_t)ld32(x + 0x14);
    if(nb < 0 || nb > 44) return 0;
    for(int lvl = 0; lvl < 3; lvl++)
        for(int32_t b = 0; b < nb; b++) {
            uint64_t ent = 0x18 + 4 * (uint64_t)(b + 13 * lvl);
            if(ent + 4 > size) continue;
            int32_t blk = (int32_t)ld32(x + ent);
            if(blk <= 0 || (uint64_t)blk + 0x68 > size) continue;
            for(int r = 0; r < 16; r++) {
                int32_t ro = (int32_t)ld32(x + blk + 0x28 + 4 * r);
                if(ro <= 0) continue;
                for(int j = 0; j < 1024; j++) {
                    uint64_t p = (uint64_t)blk + (uint32_t)ro + 2 * (uint64_t)j;
                    if(p + 2 > size || ld16(x + p) == 0xFFFF) break;
                    uint64_t fa = (uint64_t)blk + ld16(x + p) + 4;
                    if(fa + 2 > size) continue;
                    int32_t m = (int16_t)ld16(x + fa);
                    if(m < 0) continue;
                    m += delta;
                    if(m < -32768 || m >= 0) return 0;
                    x[fa] = (uint8_t)m;
                    x[fa + 1] = (uint8_t)(m >> 8);
                }
            }
        }
    return 1;
}

/* TRACK_DATA of MAPnDAT.DTR: the first five numbers ("d-50000": far plane, background rgb, background
   palette entry) of the lines after it, comment lines (*) and empty ones left out; 1, or 0 if there are fewer */
static int s2_track_data(const uint8_t *s, uint32_t size, int32_t v[5])
{
    static const char key[] = "TRACK_DATA";
    uint32_t p = 0, nv = 0;
    for(; p + sizeof key - 1 <= size; p++)
        if(!memcmp(s + p, key, sizeof key - 1)) break;
    if(p + sizeof key - 1 > size) return 0;
    while(p < size && s[p] != '\n' && s[p] != '\r') p++;                /* the rest of its line is dropped */
    while(p < size && nv < 5) {
        while(p < size && (s[p] == '\n' || s[p] == '\r' || s[p] == ' ' || s[p] == '\t')) p++;
        if(p >= size) break;
        if(s[p] == '*') {
            while(p < size && s[p] != '\n' && s[p] != '\r') p++;
            continue;
        }
        if(size - p >= 8 && !memcmp(s + p, "END_LIST", 8)) break;
        uint32_t cnt = 0;
        int32_t line[8];
        while(p < size && s[p] != '\n' && s[p] != '\r') {               /* the line's tokens */
            if(s[p] == ' ' || s[p] == '\t') { p++; continue; }
            int is_d = s[p] == 'd', neg = 0, digits = 0;
            int32_t x = 0;
            p++;
            if(is_d && p < size && s[p] == '-') { neg = 1; p++; }
            while(p < size && s[p] >= '0' && s[p] <= '9') { x = x * 10 + (s[p] - '0'); p++; digits++; }
            while(p < size && s[p] != ' ' && s[p] != '\t' && s[p] != '\n' && s[p] != '\r') p++;
            if(is_d && digits && cnt < 8) line[cnt++] = neg ? -x : x;
        }
        for(uint32_t i = 0; i < cnt && nv < 5; i++) v[nv++] = line[i];
    }
    return nv == 5;
}

/* an empty section (no camera path, no markers): at the next boundary, no bytes */
static void empty(struct build *b)
{
    b->offs[b->sections] = b->pos;
    b->counts[b->sections++] = 0;
}

static void wrf(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wr32(p, v);
}

/* CARS_DATA's row of a car model, a stand-in (the cars' physics is new in Screamer 2) that names the car by its
   livery word (team * 256 + car): char name[12], int32 livery, float maxspd, maxrpm, grip, accel, int32 automatic,
   nshift, float ratio[7] */
static void stand_in_car(uint8_t *p, const struct s2_car *c)
{
    static const float f1[4] = { 150.0f, 9000.0f, 1.0f, 1.0f }, f2[7] = { 0.0f, 3.0f, 2.0f, 1.5f, 1.2f, 1.0f, 0.8f };
    memset(p, 0, 68);
    memcpy(p, "S2 STANDIN", 10);
    wr32(p + 12, (uint32_t)c->team << 8 | c->car);
    for(int i = 0; i < 4; i++) wrf(p + 16 + 4 * i, f1[i]);
    wr32(p + 32, 1);
    wr32(p + 36, 4);
    for(int i = 0; i < 7; i++) wrf(p + 40 + 4 * i, f2[i]);
}

uint32_t track_build_s2(const struct s2_files *f, int n, uint8_t *out, uint32_t cap,
                        uint8_t *scratch, uint32_t scratch_cap, struct track_build_info *info)
{
    uint32_t t0 = now();
    struct build b = { .out = out, .scratch = scratch, .cap = cap, .pos = FIRST, .scratch_cap = scratch_cap,
                       .track = "MAP0", .info = info };
    info->fail[0] = 0;
    info->cycles = info->unpack_cycles = info->scratch = 0;
    if(n < 1 || n > 9 || cap < FIRST) return (uint32_t)fail(&b, "track number");
    b.track[3] = (char)('0' + n);
    memset(out, 0, FIRST);

    char buf[16];
    struct dat_entry e;
    uint32_t len, count = 0, dtr_n = 0, col_n = 0, mmp_n = 0, bck_n = 0;
    const uint8_t *dtr, *col, *mmp, *bck;
    /* a menu's room (no cars) has no track data: the menus' lens (STARTL.EXE 0x7f504, far plane 43,808) and colour 0 */
    int32_t td[5] = { -43808, 0, 0, 0, 0 };
    const int ncars = f->ncars;
    const int scenery = ncars == 0;         /* only the scenery: a room (the sections of cars, road points and walls stay empty) */
    if(!scenery) {
        if(!(dtr = unpack(&b, f->data, f->data_size, name(&b, "DAT.DTR", buf), &dtr_n))) return 0;
        if(!s2_track_data(dtr, dtr_n, td) || td[0] >= 0) return (uint32_t)fail(&b, "TRACK_DATA");
    }

    /* the cars' libraries: where they lie in the MLB section, how many materials they have */
    struct { const uint8_t *mlb; uint32_t size, nrec, at; } lib[S2_CARS];
    if(ncars < 0 || ncars > S2_CARS || (ncars && !f->car)) return (uint32_t)fail(&b, "cars");
    if(!dat_find(f->gfx, f->gfx_size, name(&b, ".MLB", buf), &e)) return (uint32_t)fail(&b, buf);
    uint32_t mlb_size = e.size, nmat = 0, end = pad256(mlb_size);
    for(int k = 0; k < ncars; k++) {
        lib[k].mlb = f->car[k].mlb;
        lib[k].size = f->car[k].mlb_size;
        if(!lib[k].mlb || lib[k].size < 16 || !f->car[k].x3d) return (uint32_t)fail(&b, "car");
        lib[k].nrec = mlb_records(lib[k].mlb, lib[k].size);
        lib[k].at = end;
        end += pad256(lib[k].size);
        nmat += lib[k].nrec;
    }
    /* the windows' reflection (ENV.MLB) goes after the libraries, a texture record and tiled frames */
    const uint8_t *env_mlb = 0;
    uint32_t env_n = 0, env_at = 0, env_size = 0;
    if(f->env) {
        if(!(env_mlb = unpack(&b, f->env, f->env_size, "ENV.MLB", &env_size))) return 0;
        if(!(env_n = env_frames(env_mlb, env_size))) return (uint32_t)fail(&b, "ENV.MLB");
        env_at = end;
        end += S2_ENV_HEAD + S2_ENV_FRAME * env_n;
    }
    uint32_t mlb_off = FIRST + pad256(16 * nmat);
    if((!nmat && !scenery) || mlb_off - FIRST > 32768) return (uint32_t)fail(&b, "car materials");

    /* section 0: the car materials; 1: the MLB, the cars' libraries and the reflection after it */
    if(16 * nmat > room(&b)) return (uint32_t)fail(&b, "car materials");
    for(int k = 0, o = 0; k < ncars; o += (int)lib[k].nrec, k++)
        lib_records(lib[k].mlb, lib[k].size, lib[k].nrec, lib[k].at, env_at, at(&b) + 16 * (uint32_t)o);
    if(scenery) empty(&b);
    else if(!section(&b, 16 * nmat, nmat, "car materials")) return 0;
    if(end > room(&b)) return (uint32_t)fail(&b, "MLB");
    uint8_t *m = at(&b);
    if(!extract(&b, f->gfx, f->gfx_size, &e, m, room(&b))) return 0;
    memset(m + mlb_size, 0, pad256(mlb_size) - mlb_size);
    for(int k = 0; k < ncars; k++) {
        memcpy(m + lib[k].at, lib[k].mlb, lib[k].size);
        memset(m + lib[k].at + lib[k].size, 0, pad256(lib[k].size) - lib[k].size);
        lib_relocate(m + lib[k].at, lib[k].size, lib[k].nrec, lib[k].at);
    }
    if(env_n) env_write(m + env_at, env_mlb, env_n, env_at);
    if(!section(&b, end, end, "MLB")) return 0;

    if(!dat_find(f->gfx, f->gfx_size, name(&b, ".UNR", buf), &e)) return (uint32_t)fail(&b, buf);
    if(!extract(&b, f->gfx, f->gfx_size, &e, at(&b), room(&b)) || !section(&b, e.size, e.size, buf)) return 0;
    if(!(col = unpack(&b, f->gfx, f->gfx_size, name(&b, ".COL", buf), &col_n)) ||
       !section(&b, tb_palette(col, col_n, at(&b), room(&b)), 0, "palette")) return 0;
    if(!(mmp = unpack(&b, f->gfx, f->gfx_size, name(&b, ".MMP", buf), &mmp_n))) return 0;
    len = tb_mmp_placements(mmp, mmp_n, at(&b), room(&b), &count);
    if(len) {                                   /* the cells' visibility table follows the placements in their section */
        uint32_t vis = tb_mmp_vis(mmp, mmp_n, at(&b) + len, room(&b) - len);
        len = vis ? len + vis : 0;
    }
    if(!section(&b, len, count, "placements")) return 0;
    empty(&b);                                  /* the camera path */
    len = tb_mmp_ground(mmp, mmp_n, at(&b), room(&b), &count);
    if(!section(&b, len, count, "ground index")) return 0;
    empty(&b);                                  /* the markers */
    if(room(&b) < 68 * (uint32_t)ncars) return (uint32_t)fail(&b, "cars");
    for(int k = 0; k < ncars; k++) stand_in_car(at(&b) + 68 * k, &f->car[k]);
    if(scenery) empty(&b);
    else if(!section(&b, 68 * (uint32_t)ncars, (uint32_t)ncars, "cars")) return 0;

    /* the car models: each .3DX to its place, its faces onto the car materials */
    if(scenery) empty(&b);
    else {
        uint32_t size[S2_CARS], rec = 0;
        const uint8_t *x3d[S2_CARS];
        for(int k = 0; k < ncars; k++) size[k] = f->car[k].x3d_size;
        for(int k = 0; k < ncars; rec += lib[k].nrec, k++) {
            uint32_t o = tb_car_models_at(size, ncars, k);
            if(o == 0 || size[k] == 0 || o > room(&b) || size[k] > room(&b) - o) return (uint32_t)fail(&b, "car models");
            memcpy(at(&b) + o, f->car[k].x3d, size[k]);
            x3d[k] = at(&b) + o;
            if(!faces_onto(at(&b) + o, size[k], (int32_t)(FIRST + 16 * rec) - (int32_t)mlb_off))
                return (uint32_t)fail(&b, "car faces");
        }
        if(!section(&b, tb_car_models(x3d, size, ncars, at(&b), room(&b)), (uint32_t)ncars, "car models")) return 0;
    }

    const uint8_t *dirs = at(&b);
    count = 0;
    len = tb_mmp_dirs(mmp, mmp_n, at(&b), room(&b), &count);
    if(scenery && !len) empty(&b);              /* (a room has no road: the start stays 0) */
    else {
        if(!section(&b, len, count, "road points")) return 0;
        memcpy(out + 0x5C, dirs + 4, 8);        /* the start: road point 0, its x, y and its heading */
        memcpy(out + 0x64, dirs, 4);
    }
    count = 0;
    len = tb_mmp_walls(mmp, mmp_n, at(&b), room(&b), &count);
    if(scenery && !len) empty(&b);
    else if(!section(&b, len, count, "walls")) return 0;
    if(room(&b) < 856) return (uint32_t)fail(&b, "race screen");
    memset(at(&b), 0, 856);                     /* no race screen: 0 images, 0 fonts, the pictures' boxes and texts */
    if(!section(&b, 856, 856, "race screen")) return 0;
    uint32_t colour = (uint32_t)td[4] & 255;
    if(room(&b) < TB_BACKDROP_SIZE) return (uint32_t)fail(&b, "backdrop");
    if(dat_find(f->gfx, f->gfx_size, name(&b, ".BCK", buf), &e)) {
        if(!(bck = unpack(&b, f->gfx, f->gfx_size, buf, &bck_n)) ||
           !section(&b, tb_backdrop(bck, bck_n, at(&b), room(&b)), 0, "backdrop")) return 0;
    } else {                                    /* track 7 is indoors: the colour alone */
        memset(at(&b), (int)colour, 512 * 256);
        memcpy(at(&b) + 512 * 256, at(&b) + 512 * 255, 512);
        if(!section(&b, TB_BACKDROP_SIZE, 0, "backdrop")) return 0;
    }
    if(room(&b) < 8) return (uint32_t)fail(&b, "opponents");
    memset(at(&b), 0, 8);                       /* no opponents */
    if(!section(&b, 8, 8, "opponents")) return 0;
    len = tb_mmp_kinds(mmp, mmp_n, at(&b), room(&b), &count);      /* the placements' road kinds, for the car model */
    if(!section(&b, len, count, "road kinds")) return 0;

    /* the header, offsets and counts as make_track_s2.py packs them (the sections from the MLB on), then the CRCs */
    const uint32_t *o = b.offs + 1, *c = b.counts + 1;
    uint32_t size = b.pos, blocks = (size - FIRST + BLOCK - 1) / BLOCK;
    if(blocks > MAX_BLOCKS) return (uint32_t)fail(&b, "image size");
    memcpy(out, "SCRK", 4);
    wr32(out + 0x04, 7);
    wr32(out + 0x08, o[0]);
    wr32(out + 0x0C, c[0]);
    wr32(out + 0x10, o[1]);
    wr32(out + 0x14, c[1]);
    wr32(out + 0x18, o[2]);
    for(int k = 3; k <= 10; k++) {
        wr32(out + 0x1C + 8 * (k - 3), o[k]);
        wr32(out + 0x20 + 8 * (k - 3), c[k]);
    }
    wr32(out + 0x68, o[11]);
    wr32(out + 0x6C, c[11]);
    wr32(out + 0x70, (uint32_t)-td[0]);         /* the far plane, positive */
    wr32(out + 0x74, o[14]);                    /* the road kinds, one byte a placement */
    wr32(out + 0x78, o[12]);
    wr32(out + 0x7C, colour);
    wr32(out + 0x80, o[13]);
    wr32(out + 0x84, c[13]);
    wr32(out + 0x88, size);
    wr32(out + 0x8C, blocks);
    for(uint32_t k = 0; k < blocks; k++) {
        uint32_t s = FIRST + k * BLOCK, z = size - s < BLOCK ? size : s + BLOCK;
        wr32(out + 0x90 + 4 * k, BLOCK_CRC(out + s, out + z));
    }
    info->cycles = now() - t0;
    info->scratch = b.used;
    return size;
}
