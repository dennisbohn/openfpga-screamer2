/*
 * Host test of the track maps' cut blocks (firmware/s2trackmap.c, s2map_cut()), run by tools/s2/check_map_s2.sh: GFX/MAPS1.DAT of the game's folder (a file), its seven meshes
 * unpacked by the firmware's reader (dat.c), every block cut for the face rows 0 .. 7 into pieces of at most 512 vertices, and the pieces compared with the block they come from by
 * a reading of the 3DX format of its own here:
 *   - a block of at most 512 vertices is not cut and not copied (the pointers are the original's);
 *   - a piece has at most 512 vertices, its header the block's (middle, flags) but for the counts and the offsets, no position record (the caller places it), normals none, and
 *     all 16 rows the one list of its faces;
 *   - the corners of every face are inside the piece's vertices;
 *   - for each of the rows 0 .. 7 the faces of the pieces, one piece after the other, are exactly the faces of the block's row in the row's order (the same number, and each face's
 *     colour, material, normal and every corner's coordinates and texture coordinates equal): drawn in turn they paint what the whole block paints, in the same order.
 * The six blocks over 512 vertices (AMAP1's second, AMAP4's first three, AMAP5's first, AMAP6's second), how many pieces each gives per row and the bytes all of them take are printed.
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

/* the stand-ins s2scene.c and track.c would bring: this test links only the cut */
uint8_t *s2scene_models(void) { return 0; }
uint32_t s2scene_generation(void) { return 0; }
int s2scene_open(struct zip *z, struct s2_cont *c, const char *path) { (void)z; (void)c; (void)path; return 0; }
uint32_t s2scene_entry(struct s2_cont *c, const char *name, uint8_t *out, uint32_t max) { (void)c; (void)name; (void)out; (void)max; return 0; }
void s2scene_begin(const uint8_t *mlb, int buffer, int fresh) { (void)mlb; (void)buffer; (void)fresh; }
void s2scene_mlb(const uint8_t *mlb) { (void)mlb; }
void s2scene_blocks(const struct track_cam *cam, const struct track_block *b, int n) { (void)cam; (void)b; (void)n; }
void s2scene_end(void) {}
float s2scene_dist2(const struct track_cam *cam, float x, float y, float z) { (void)cam; (void)x; (void)y; (void)z; return 0; }
void track_block_piece(const struct track_cam *cam, const struct track_block *b, struct gu_piece *g) { (void)cam; (void)b; (void)g; }
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
    static uint8_t mesh[0x40000], dst[0x200000];
    static struct s2map_block pc;
    int cut_blocks = 0, total_blocks = 0, most_chunks = 0;
    uint32_t used = 0;
    for (int n = 1; n <= S2MAP_TRACKS; n++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('0' + n);
        struct dat_entry e;
        CHECK(dat_find(file, size, name, &e), "%s is in the container", name);
        const uint32_t got = dat_extract(file, size, &e, mesh, sizeof mesh);
        CHECK(got == e.size && got > 0x100, "%s unpacked (%u bytes)", name, got);
        const int blocks = rd32(mesh + 0x14);
        CHECK(blocks >= 1 && blocks <= S2MAP_BLOCKS, "%s has %d blocks", name, blocks);
        for (int k = 0; k < blocks; k++) {
            const uint8_t *blk = mesh + rd32(mesh + 0x18 + 4 * k);
            const int nv = rd32(blk + 0x10), nf = rd32(blk + 0x14);
            const uint32_t before = used;
            const int ok = s2map_cut(mesh, k, 512, dst, sizeof dst, &used, &pc);
            total_blocks++;
            if (getenv("MAP_INFO") && ok) {                 /* MAP_INFO=1: every block's name, header centre (+4) and place: the point whose distance orders the blocks */
                char nm[9] = { 0 };
                memcpy(nm, blk + 0x68, 8);
                printf("  info %s block %d '%s': %d vertices, header centre (%.0f %.0f %.0f), place (%.0f %.0f %.0f)\n", name, k, nm, nv, pc.c[0], pc.c[1], pc.c[2], pc.t[0], pc.t[1], pc.t[2]);
                CHECK(pc.c[0] == 0.0f && pc.c[1] == 0.0f && pc.c[2] == 0.0f, "%s block %d: the header's centre is 0 (the key is the block's place)", name, k);
            }
            CHECK(ok, "%s block %d: cut", name, k);
            if (!ok) continue;
            CHECK(pc.file == mesh && pc.blk == blk, "%s block %d: the block is the mesh's", name, k);
            if (nv <= 512) {
                CHECK(!pc.cut && used == before, "%s block %d (%d vertices) is not cut and not copied", name, k, nv);
                continue;
            }
            cut_blocks++;
            CHECK(pc.cut, "%s block %d (%d vertices) is cut", name, k, nv);
            printf("  %s block %d: %d vertices, %d faces -> pieces per row:", name, k, nv, nf);
            static uint16_t orig[1024], mine[1024];
            struct sig *osig = malloc(sizeof(struct sig) * 1024);
            for (int r = 0; r < S2MAP_ROWS; r++) {
                const int no = row_list(blk, r, orig, 1024);
                const int np = pc.nchunks[r];
                printf(" %d", np);
                if (np > most_chunks) most_chunks = np;
                if (no <= 0) {
                    CHECK(np == 0, "%s block %d row %d: the block has no row (or none to draw), the pieces %d", name, k, r, np);
                    continue;
                }
                CHECK(np >= 1 && np <= S2MAP_CHUNKS, "%s block %d row %d: %d pieces", name, k, r, np);
                for (int i = 0; i < no; i++) signature(mesh, blk, blk + orig[i], &osig[i]);
                int at = 0;                                 /* the next face of the block's row */
                for (int p = 0; p < np; p++) {
                    const uint8_t *pb = pc.chunk[r][p];
                    const int pv = rd32(pb + 0x10), pf = rd32(pb + 0x14);
                    CHECK(pv >= 1 && pv <= 512, "%s block %d row %d piece %d: %d vertices", name, k, r, p, pv);
                    CHECK(rd32(pb + 0x0C) == 0 && rd32(pb + 0x20) == -1 && !(rd32(pb + 0x18) & 0x2000), "%s block %d row %d piece %d: no position record, no normals", name, k, r, p);
                    CHECK(rd32(pb + 0x18) == rd32(blk + 0x18) && !memcmp(pb + 4, blk + 4, 6), "%s block %d row %d piece %d: flags and middle are the block's", name, k, r, p);
                    const int nm = row_list(pb, 0, mine, 1024);
                    CHECK(nm == pf && nm >= 1, "%s block %d row %d piece %d: its list has %d faces of %d", name, k, r, p, nm, pf);
                    for (int q = 1; q < 16; q++) {
                        static uint16_t other[1024];
                        const int nq = row_list(pb, q, other, 1024);
                        CHECK(nq == nm && !memcmp(other, mine, sizeof(uint16_t) * (size_t)(nm > 0 ? nm : 0)), "%s block %d row %d piece %d: row %d is not the one list", name, k, r, p, q);
                    }
                    for (int i = 0; i < nm; i++) {
                        const uint8_t *fc = pb + mine[i];
                        for (int j = 0; j < rd16(fc + 6) && j < 4; j++) CHECK(ru16(fc + 8 + 2 * j) / 6 < pv && ru16(fc + 8 + 2 * j) % 6 == 0, "%s block %d row %d piece %d: a corner is outside", name, k, r, p);
                        struct sig s;
                        signature(pb, pb, fc, &s);
                        CHECK(at < no && !memcmp(&osig[at], &s, sizeof s), "%s block %d row %d piece %d face %d is not the block's face %d", name, k, r, p, i, at);
                        at++;
                    }
                }
                CHECK(at == no, "%s block %d row %d: the pieces have %d faces, the block's row %d", name, k, r, at, no);
            }
            printf("\n");
            free(osig);
        }
    }
    CHECK(cut_blocks == 6, "six blocks over 512 vertices are cut (%d of %d)", cut_blocks, total_blocks);
    printf("%d checks, %d failures, %d of %d blocks cut, at most %d pieces a row, %u KB of pieces\n", checks, failures, cut_blocks, total_blocks, most_chunks, used >> 10);
    return failures != 0;
}
