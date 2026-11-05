/*
 * Host test of the track maps' cut blocks (firmware/s2trackmap.c, s2map_cut()), run by tools/s2/check_map_s2.sh: GFX/MAPS1.DAT of the game's folder (a file), its seven meshes
 * unpacked by the firmware's reader (dat.c), every block cut into pieces of at most 512 vertices, and the pieces compared with the block they come from by a reading of the
 * 3DX format of its own here:
 *   - a block of at most 512 vertices is its own piece, not copied (the pointer is the original's);
 *   - a piece has at most 512 vertices, its header the block's (middle, flags) but for the counts and the offsets, no position record (the caller places it), normals none;
 *   - the corners of every face are inside the piece's vertices; the face's colour, material, normal and every corner's coordinates and texture coordinates equal the original's;
 *   - for each of the 16 rows (the face orders of the eye's sides) the faces of the pieces, each in its own order, are the faces of the block's row: the same number in all, and the
 *     list of a piece is a subsequence of the block's (so that no face is lost, doubled or turned over another of its piece's);
 *   - the sum of the pieces' faces is the block's.
 * The six blocks over 512 vertices (AMAP1's second, AMAP4's first three, AMAP5's first, AMAP6's second) and how many pieces each gives are printed.
 *
 * Usage: check_map_s2 <game folder>
 */
#include "../../firmware/s2trackmap.h"
#include "../../firmware/dat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 30) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rd16(const uint8_t *p) { return (int16_t)ru16(p); }

/* the stand-ins s2scene.c would bring: this test links only the cut */
uint8_t *s2scene_models(void) { return 0; }
uint32_t s2scene_generation(void) { return 0; }
int s2scene_open(struct zip *z, struct s2_cont *c, const char *path) { (void)z; (void)c; (void)path; return 0; }
uint32_t s2scene_entry(struct s2_cont *c, const char *name, uint8_t *out, uint32_t max) { (void)c; (void)name; (void)out; (void)max; return 0; }
void s2scene_begin(const uint8_t *mlb, int buffer, int fresh) { (void)mlb; (void)buffer; (void)fresh; }
void s2scene_mlb(const uint8_t *mlb) { (void)mlb; }
void s2scene_blocks(const struct track_cam *cam, const struct track_block *b, int n) { (void)cam; (void)b; (void)n; }
void s2scene_end(void) {}
float s2scene_dist2(const struct track_cam *cam, float x, float y, float z) { (void)cam; (void)x; (void)y; (void)z; return 0; }
void dlog_str(const char *s) { (void)s; }
void dlog_dec(int32_t v) { (void)v; }

/* a face as one comparable record: colour, material, the normal, the corners (the vertex's x, y, z and the texture u, v) */
struct sig { int32_t head[2 + 3 + 1]; int16_t c[4][5]; };

static void signature(const uint8_t *obj, const uint8_t *blk, const uint8_t *fc, struct sig *s)
{
    const uint8_t *verts = obj + rd32(blk + 0x1C), *uvs = obj + rd32(blk + 0x24);
    memset(s, 0, sizeof *s);
    s->head[0] = ru16(fc + 2);
    s->head[1] = ru16(fc + 4);
    for (int k = 0; k < 3; k++) s->head[2 + k] = rd16(fc + 0x14 + 4 * k);
    s->head[5] = rd16(fc + 6);
    for (int j = 0; j < s->head[5] && j < 4; j++) {
        const int vo = ru16(fc + 8 + 2 * j);
        for (int k = 0; k < 3; k++) s->c[j][k] = rd16(verts + vo + 2 * k);
        s->c[j][3] = rd16(uvs + vo);
        s->c[j][4] = rd16(uvs + vo + 2);
    }
}

static int row_list(const uint8_t *blk, int r, uint16_t *out, int max)
{
    const int32_t ro = rd32(blk + 0x28 + 4 * r);
    if (ro <= 0) return -1;
    int n = 0;
    for (const uint8_t *e = blk + ro; ru16(e) != 0xFFFF; e += 2) {
        if (n >= max) return -2;
        out[n++] = ru16(e);
    }
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <game folder>\n", argv[0]); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/GFX/MAPS1.DAT", argv[1]);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    const uint32_t size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(size + 16);
    if (fread(file, 1, size, f) != size) return 2;
    fclose(f);
    static uint8_t mesh[0x40000], dst[0x100000];
    static struct s2map_piece pc[S2MAP_PIECES];
    int cut_blocks = 0, total_blocks = 0;
    for (int n = 1; n <= S2MAP_TRACKS; n++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('0' + n);
        struct dat_entry e;
        CHECK(dat_find(file, size, name, &e), "%s is in the container", name);
        const uint32_t got = dat_extract(file, size, &e, mesh, sizeof mesh);
        CHECK(got == e.size && got > 0x100, "%s unpacked (%u bytes)", name, got);
        const int blocks = rd32(mesh + 0x14);
        CHECK(blocks >= 1 && blocks <= 4, "%s has %d blocks", name, blocks);
        for (int k = 0; k < blocks; k++) {
            const uint8_t *blk = mesh + rd32(mesh + 0x18 + 4 * k);
            const int nv = rd32(blk + 0x10), nf = rd32(blk + 0x14);
            uint32_t used = 0;
            const int np = s2map_cut(mesh, k, 512, dst, sizeof dst, &used, pc, S2MAP_PIECES);
            total_blocks++;
            CHECK(np >= 1, "%s block %d: cut", name, k);
            if (np < 1) continue;
            if (nv <= 512) {
                CHECK(np == 1 && pc[0].blk == blk && pc[0].file == mesh && used == 0, "%s block %d (%d vertices) is its own piece, not copied", name, k, nv);
                continue;
            }
            cut_blocks++;
            printf("  %s block %d: %d vertices, %d faces -> %d pieces:", name, k, nv, nf, np);
            int faces_sum = 0;
            static uint16_t orig[1024], mine[1024];
            struct sig *osig = malloc(sizeof(struct sig) * 1024);
            for (int p = 0; p < np; p++) {
                const uint8_t *pb = pc[p].blk;
                const int pv = rd32(pb + 0x10), pf = rd32(pb + 0x14);
                printf(" %dv/%df", pv, pf);
                CHECK(pc[p].file == pb && pv >= 1 && pv <= 512, "%s block %d piece %d: %d vertices", name, k, p, pv);
                CHECK(rd32(pb + 0x0C) == 0 && rd32(pb + 0x20) == -1 && !(rd32(pb + 0x18) & 0x2000), "%s block %d piece %d: no position record, no normals", name, k, p);
                CHECK(rd32(pb + 0x18) == rd32(blk + 0x18) && !memcmp(pb + 4, blk + 4, 6), "%s block %d piece %d: flags and middle are the block's", name, k, p);
                faces_sum += pf;
                /* every face's corners are inside the piece */
                int n0 = row_list(pb, 0, mine, 1024);
                CHECK(n0 == pf, "%s block %d piece %d: row 0 has %d faces of %d", name, k, p, n0, pf);
                for (int i = 0; i < n0; i++) {
                    const uint8_t *fc = pb + mine[i];
                    for (int j = 0; j < rd16(fc + 6) && j < 4; j++) CHECK(ru16(fc + 8 + 2 * j) / 6 < pv && ru16(fc + 8 + 2 * j) % 6 == 0, "%s block %d piece %d: a corner is outside", name, k, p);
                }
            }
            CHECK(faces_sum == nf, "%s block %d: the pieces have %d faces of %d", name, k, faces_sum, nf);
            printf("\n");
            /* the rows */
            for (int r = 0; r < 16; r++) {
                const int no = row_list(blk, r, orig, 1024);
                if (no < 0) {
                    for (int p = 0; p < np; p++) CHECK(row_list(pc[p].blk, r, mine, 1024) == -1, "%s block %d row %d: a piece has a row the block has not", name, k, r);
                    continue;
                }
                for (int i = 0; i < no; i++) signature(mesh, blk, blk + orig[i], &osig[i]);
                int total = 0;
                for (int p = 0; p < np; p++) {
                    const uint8_t *pb = pc[p].blk;
                    const int nm = row_list(pb, r, mine, 1024);
                    CHECK(nm >= 0, "%s block %d row %d piece %d: no row", name, k, r, p);
                    if (nm < 0) continue;
                    total += nm;
                    int at = 0, ok = 1;
                    for (int i = 0; i < nm && ok; i++) {
                        struct sig s;
                        signature(pb, pb, pb + mine[i], &s);
                        while (at < no && memcmp(&osig[at], &s, sizeof s)) at++;
                        if (at >= no) ok = 0;
                        at++;
                    }
                    CHECK(ok, "%s block %d row %d piece %d: its faces are not in the block's order", name, k, r, p);
                }
                CHECK(total == no, "%s block %d row %d: the pieces have %d faces, the block %d", name, k, r, total, no);
            }
            free(osig);
        }
    }
    CHECK(cut_blocks == 6, "six blocks over 512 vertices are cut (%d of %d)", cut_blocks, total_blocks);
    printf("%d checks, %d failures, %d of %d blocks cut\n", checks, failures, cut_blocks, total_blocks);
    return failures != 0;
}
