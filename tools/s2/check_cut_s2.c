/*
 * Host test of the geometry driver's cut of the blocks that are over the unit's limits (firmware/geom.c: gu_oversize, gu_split, gu_cut; geom.h), run by tools/s2/check_cut_s2.sh
 * on the game's own data, unpacked by the firmware's reader (dat.c) and read by a reading of the 3DX format of its own here: the track maps (GFX/MAPS1.DAT and MAPS2.DAT, six blocks over 512
 * vertices each), the arcade scene (ANIMS/SCENE011.DAT and SCENE012.DAT, one block of 515), a car, a race track.
 *   - with the unit's limits, every block, every one of the 16 face rows: a block inside the limits is one run with the row's faces, one over them is cut;
 *   - with limits far smaller than any block (48 vertices, 17 faces), every block of every object of these files, so that the cut is tried on thousands of blocks of all kinds;
 *   in each case, for every run: at most the limits' vertices and faces, the header the block's (middle, flags) but for the counts and the offsets, no position record, normals
 *   none, all 16 rows the one list, every corner inside the run's vertices; and the runs one after the other hold exactly the faces of the block's row in the row's order (same
 *   number, and each face's colour, material, normal and every corner's position and texture coordinates equal): drawn in turn they paint what the whole block paints.
 *   - gu_split(): the pieces it gives are copies of the piece with the run as obj and blk; asking again gives the same runs (no new cut); more (block, row) pairs than slots give
 *     the first up and it is cut again, to the same faces; gu_oversize() is true for the blocks over the limits only; nothing failed (gu_cut_failures 0).
 * Prints how many blocks are over the limits and the most memory the runs of one (block, row) take (GU_CUT_BYTES must hold it).
 *
 * Usage: check_cut_s2 <game folder>
 */
#include "../../firmware/geom.h"
#include "../../firmware/dat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (failures < 30) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rd16(const uint8_t *p) { return (int16_t)ru16(p); }

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

#define ROW_MAX 4096
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

static uint32_t run_bytes(const uint8_t *pb) { return (uint32_t)(rd32(pb + 0x24) + 6 * rd32(pb + 0x10) + 3) & ~3u; }

static uint32_t most_bytes;
static uint8_t dst[0x400000];
#define STRESS_RUNS 200

/* the runs `out` (n of them, from dst) of row r of the block against the block's own row; `what` names the case for the messages */
static void verify(const char *what, const uint8_t *mesh, const uint8_t *blk, int r, int max_vert, int max_faces, int n, const uint8_t *out[])
{
    static uint16_t orig[ROW_MAX], mine[ROW_MAX], other[ROW_MAX];
    static struct sig osig[ROW_MAX];
    const int no = row_list(blk, r, orig, ROW_MAX);
    if (no <= 0) {
        CHECK(n == 0, "%s row %d: the block has no faces in the row, the runs %d", what, r, n);
        return;
    }
    CHECK(n >= 1, "%s row %d: no run for %d faces", what, r, no);
    for (int i = 0; i < no; i++) signature(mesh, blk, blk + orig[i], &osig[i]);
    int at = 0;
    for (int p = 0; p < n; p++) {
        const uint8_t *pb = out[p];
        const int pv = rd32(pb + 0x10), pf = rd32(pb + 0x14);
        CHECK(pv >= 1 && pv <= max_vert, "%s row %d run %d: %d vertices (limit %d)", what, r, p, pv, max_vert);
        CHECK(pf >= 1 && pf <= max_faces, "%s row %d run %d: %d faces (limit %d)", what, r, p, pf, max_faces);
        CHECK(rd32(pb + 0x0C) == 0 && rd32(pb + 0x20) == -1 && !(rd32(pb + 0x18) & 0x2000), "%s row %d run %d: no position record, no normals", what, r, p);
        CHECK(rd32(pb + 0x18) == rd32(blk + 0x18) && !memcmp(pb + 4, blk + 4, 6), "%s row %d run %d: flags and middle are the block's", what, r, p);
        const int nm = row_list(pb, 0, mine, ROW_MAX);
        CHECK(nm == pf, "%s row %d run %d: its list has %d faces of %d", what, r, p, nm, pf);
        for (int q = 1; q < 16; q++) {
            const int nq = row_list(pb, q, other, ROW_MAX);
            CHECK(nq == nm && !memcmp(other, mine, sizeof(uint16_t) * (size_t)(nm > 0 ? nm : 0)), "%s row %d run %d: row %d is not the one list", what, r, p, q);
        }
        for (int i = 0; i < nm && i < pf; i++) {
            const uint8_t *fc = pb + mine[i];
            for (int j = 0; j < rd16(fc + 6) && j < 4; j++) CHECK(ru16(fc + 8 + 2 * j) / 6 < pv && ru16(fc + 8 + 2 * j) % 6 == 0, "%s row %d run %d: a corner is outside", what, r, p);
            struct sig s;
            signature(pb, pb, fc, &s);
            CHECK(at < no && !memcmp(&osig[at], &s, sizeof s), "%s row %d run %d face %d is not the block's face %d", what, r, p, i, at);
            at++;
        }
    }
    CHECK(at == no, "%s row %d: the runs have %d faces, the block's row %d", what, r, at, no);
    if (n > 0 && max_vert == GU_MAX_VERT && out[n - 1] >= dst && out[n - 1] < dst + sizeof dst) {      /* (runs cut into dst here, not those of gu_split's slots) */
        const uint32_t bytes = (uint32_t)(out[n - 1] - dst) + run_bytes(out[n - 1]);
        if (bytes > most_bytes) most_bytes = bytes;
    }
}

struct object { const uint8_t *mesh; uint32_t off; };
static struct object objs[8192];
static int nobj;
static int over_blocks, real_blocks, stress_blocks;
static const uint8_t *over_seen[64], *over_mesh[64];                 /* the blocks over the limits and their meshes */

static const uint8_t *load(const uint8_t *file, uint32_t size, const char *name, uint32_t *len)
{
    struct dat_entry e;
    if (!dat_find(file, size, name, &e)) return 0;
    uint8_t *out = malloc(e.size + 16);
    if (dat_extract(file, size, &e, out, e.size + 16) != e.size) return 0;
    *len = e.size;
    return out;
}

static uint8_t *read_file(const char *dir, const char *rel, uint32_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot read %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    *size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*size + 16);
    if (fread(d, 1, *size, f) != *size) return 0;
    fclose(f);
    return d;
}

static void add_3dx(const uint8_t *file, uint32_t size, const char *name)
{
    uint32_t n;
    const uint8_t *mesh = load(file, size, name, &n);
    CHECK(mesh != 0 && n > 0x100, "%s is in the container", name);
    if (mesh && nobj < 8192) objs[nobj++] = (struct object){ mesh, 0 };
}

static void add_unr(const uint8_t *file, uint32_t size, const char *name)
{
    uint32_t n;
    const uint8_t *unr = load(file, size, name, &n);
    CHECK(unr != 0, "%s is in the container", name);
    if (!unr) return;
    const int count = rd32(unr + 4);
    for (int k = 0; k < count && nobj < 8192; k++) objs[nobj++] = (struct object){ unr, (uint32_t)(8 + 4 * count + rd32(unr + 8 + 4 * k)) };
}

/* every block of every level of detail of the objects (entries 0 .. 38 of the table at +0x18), each distinct block once */
static void run_blocks(const char *what, int first, int last, int max_vert, int max_faces, int real)
{
    static const uint8_t *seen[200000];
    static int nseen;
    if (first == 0) nseen = 0;
    for (int o = first; o < last; o++) {
        const uint8_t *mesh = objs[o].mesh, *ob = mesh + objs[o].off;
        for (int e = 0; e < 39; e++) {
            const int32_t off = rd32(ob + 0x18 + 4 * e);
            if (off <= 0) continue;
            const uint8_t *blk = ob + off;
            int dup = 0;
            for (int s = 0; s < nseen && !dup; s++) dup = seen[s] == blk;
            if (dup || nseen >= 200000) continue;
            seen[nseen++] = blk;
            const int nv = rd32(blk + 0x10), nf = rd32(blk + 0x14);
            if (nv < 1 || nv > 4096 || (rd32(blk + 0x18) & 0x2000)) continue;
            const int over = nv > GU_MAX_VERT || nf > GU_MAX_FACES;
            if (real) real_blocks++;
            else stress_blocks++;
            if (real && over) {
                if (over_blocks < 64) {
                    over_seen[over_blocks] = blk;
                    over_mesh[over_blocks] = mesh;
                }
                over_blocks++;
                printf("  %s: a block of %d vertices and %d faces is over the limits\n", what, nv, nf);
            }
            for (int r = 0; r < 16; r++) {
                const uint8_t *out[STRESS_RUNS];
                const int n = gu_cut(mesh, blk, r, max_vert, max_faces, dst, sizeof dst, out, STRESS_RUNS);
                CHECK(n >= 0, "%s: a block of %d vertices row %d could not be cut (limits %d, %d)", what, nv, r, max_vert, max_faces);
                if (n >= 0) verify(what, mesh, blk, r, max_vert, max_faces, n, out);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <game folder>\n", argv[0]); return 2; }
    uint32_t size;
    uint8_t *maps = read_file(argv[1], "GFX/MAPS1.DAT", &size);
    if (!maps) return 2;
    const uint32_t maps_size = size;
    for (int n = 1; n <= 7; n++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('0' + n);
        add_3dx(maps, maps_size, name);
    }
    uint32_t size2;
    uint8_t *maps2 = read_file(argv[1], "GFX/MAPS2.DAT", &size2);       /* the second set of the maps: the rows are the longest (775 faces) */
    if (!maps2) return 2;
    for (int n = 1; n <= 7; n++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('0' + n);
        add_3dx(maps2, size2, name);
    }
    const int n_maps = nobj;
    uint8_t *scene1 = read_file(argv[1], "ANIMS/SCENE011.DAT", &size);
    if (!scene1) return 2;
    add_unr(scene1, size, "MAP3.UNR");
    const int n_scene1 = nobj;
    uint8_t *scene2 = read_file(argv[1], "ANIMS/SCENE012.DAT", &size);
    if (!scene2) return 2;
    add_unr(scene2, size, "MAP3.UNR");
    const int n_scene2 = nobj;
    uint8_t *car = read_file(argv[1], "GFX/CARS101.DAT", &size);
    if (!car) return 2;
    add_3dx(car, size, "AUTO1.3DX");
    uint8_t *track = read_file(argv[1], "GFX/MAP3H01.DAT", &size);
    if (!track) return 2;
    add_unr(track, size, "MAP3.UNR");
    printf("%d objects (maps %d, scene files %d and %d, a car, a track)\n", nobj, n_maps, n_scene1 - n_maps, n_scene2 - n_scene1);

    /* the unit's limits on the maps and the scene files, then limits far below any block on everything */
    run_blocks("maps", 0, n_maps, GU_MAX_VERT, GU_MAX_FACES, 1);
    run_blocks("scene 1", n_maps, n_scene1, GU_MAX_VERT, GU_MAX_FACES, 1);
    run_blocks("scene 2", n_scene1, n_scene2, GU_MAX_VERT, GU_MAX_FACES, 1);
    run_blocks("car and track", n_scene2, nobj, GU_MAX_VERT, GU_MAX_FACES, 1);
    run_blocks("stress", 0, nobj, 48, 17, 0);
    CHECK(over_blocks >= 7 && over_blocks < 64, "the maps' six blocks and the scene's block are over the limits (%d blocks, each counted once)", over_blocks);

    /* gu_split and its slots on the blocks over the limits */
    int pairs = 0, first_n = 0;
    for (int b = 0; b < over_blocks && b < 64; b++) {
        const uint8_t *blk = over_seen[b], *mesh = over_mesh[b];
        for (int r = 0; r < 16; r++) {
            struct gu_piece g = { .row = r, .mirror = r & 1, .phase = 0x1234u + (uint32_t)r, .refl = 0xA5A5u, .obj = mesh, .blk = blk };
            for (int k = 0; k < 9; k++) g.m[k] = 1000 * k + r;
            for (int k = 0; k < 3; k++) { g.t[k] = 77 * k - r; g.e[k] = 5 * k + r; }
            CHECK(gu_oversize(&g), "gu_oversize is true for the block over the limits");
            struct gu_piece run[GU_CUT_RUNS], again[GU_CUT_RUNS];
            const int n = gu_split(&g, run);
            const uint8_t *ptr[GU_CUT_RUNS];
            for (int i = 0; i < n; i++) {
                ptr[i] = run[i].blk;
                CHECK(run[i].obj == run[i].blk && run[i].row == g.row && run[i].mirror == g.mirror && run[i].phase == g.phase && run[i].refl == g.refl &&
                      !memcmp(run[i].m, g.m, sizeof g.m) && !memcmp(run[i].t, g.t, sizeof g.t) && !memcmp(run[i].e, g.e, sizeof g.e), "a run is a copy of the piece with its own obj and blk");
            }
            verify("gu_split", mesh, blk, r, GU_MAX_VERT, GU_MAX_FACES, n, ptr);
            const int n2 = gu_split(&g, again);
            CHECK(n2 == n, "asking again gives the same number of runs (%d, %d)", n2, n);
            for (int i = 0; i < n2 && i < n; i++) CHECK(again[i].blk == run[i].blk, "asking again gives the same runs, not a new cut");
            if (pairs == 0) first_n = n;
            pairs++;
        }
    }
    /* the first (block, row) was given up by now (more pairs than slots): it is cut again, to the same faces */
    CHECK(pairs > GU_CUT_SLOTS, "more (block, row) pairs than slots (%d, %d)", pairs, GU_CUT_SLOTS);
    {
        struct gu_piece g = { .row = 0, .obj = over_mesh[0], .blk = over_seen[0] }, run[GU_CUT_RUNS];
        const int n = gu_split(&g, run);
        const uint8_t *ptr[GU_CUT_RUNS];
        for (int i = 0; i < n; i++) ptr[i] = run[i].blk;
        CHECK(n == first_n, "cut again after being given up: %d runs (%d)", n, first_n);
        verify("gu_split again", over_mesh[0], over_seen[0], 0, GU_MAX_VERT, GU_MAX_FACES, n, ptr);
    }
    /* a block inside the limits is not oversize */
    {
        const uint8_t *ob = objs[n_scene2].mesh + objs[n_scene2].off;          /* the car's object */
        const uint8_t *blk = ob + rd32(ob + 0x18);
        struct gu_piece g = { .obj = ob, .blk = blk };
        CHECK(!gu_oversize(&g), "gu_oversize is false for a car's block");
    }
    CHECK(gu_cut_failures == 0, "nothing could not be cut (%u)", gu_cut_failures);
    CHECK(most_bytes <= GU_CUT_BYTES, "the runs of one (block, row) take %u bytes, a slot is %u", most_bytes, (unsigned)GU_CUT_BYTES);
    printf("%d checks, %d failures: %d blocks looked at with the unit's limits (%d over them), %d with the stress limits; the runs of one (block, row) take %u bytes at most (a slot: %u)\n",
           checks, failures, real_blocks, over_blocks, stress_blocks, most_bytes, (unsigned)GU_CUT_BYTES);
    return failures != 0;
}
