/*
 * The track (track.h). The file (Screamer 2: tools/s2/make_track_s2.py, "SCRK" version 7; Screamer 1's
 * was tools/make_track.py, version 6): the car materials, MAPn.MLB with the cars' libraries after it
 * and MAPn.UNR unchanged, the palette, the placements (then the cells' visibility table, VIS_MAGIC), the
 * camera path (empty), the ground index, the markers (empty), the cars, the car models, road points and
 * walls, the start, the race screen (empty), the backdrop and its ground colour, the opponents (empty);
 * the far plane in the header.
 *
 * The game's structures (openscreamer docs/NOTES.md, "How the track is built"):
 *   UNR    int32 blob size, int32 count, count offsets into the blob; an object's block at
 *          object + [object + 0x18]; block +0x04 its middle (int16 x, y, z), +0x10 vertex
 *          count, +0x1C vertex array and +0x24 texture coordinates (both from the object, 6
 *          bytes a vertex: int16 x, y up, z and int16 u, v, 0), +0x28..+0x64 sixteen face
 *          rows (from the block, uint16 face offsets, 0xFFFF ends a row), +0x68 its name
 *   face   +0x02 colour (routine 128), +0x04 material (byte offset into MLB), +0x06 corners
 *          (3 or 4), +0x08 corner vertex byte offsets, +0x14/+0x18/+0x1C normal (16384 = 1),
 *          pointing away from the side it is seen from: seen when (corner - eye) . N >= 0
 *   MLB    material +0 routine (16 texture, 64 texel 0 see-through, 128 one colour, 224 see-
 *          through through MAPn.SMG), +4 texture record, +8/+12 size in texels; texture
 *          record +8 the pixels of frame 0, rows of 256 bytes; texel = raw u, v * size / 256
 *   piece  world = (X + x cos a - z sin a, Y + x sin a + z cos a, height + y), a = T * 2 pi / 2048
 * A block's sixteen rows are its faces in drawing order, back to front, each sorted for one
 * direction of the eye in the block's frame (face_eye, the game's 0x8a598): rows 0..7 for an
 * eye above the block's x-z plane, 8..15 below it, and in each half one row per 45-degree
 * sector around the block's y axis, starting at +z and turning towards +x. A row holds all
 * the faces, or nearly (the data); the normal test leaves out those turned away.
 */
#include "track.h"
#ifdef TRACK_DUMP
#include <stdio.h>
#endif
#include "crc32.h"
#include "prof.h"
#include <math.h>
#include <string.h>

#define BLOCK       0x40000     /* bytes per CRC block (tools/make_track.py) */
#define MAX_PIECES  4096        /* placements (Screamer 2's H01 set): 1099 to 3742 on its seven tracks */
#define MAX_OBJ     1024        /* meshes; 345 to 705 on a track */
#define MAX_BLOCKS  16          /* blocks of a mesh drawn (a car has twelve, track 1's football helmet two) */

/* How far Screamer 2 draws at DETAIL HIGH (docs/screamer2/performance.md, section 6; STARTL.EXE
   0x7f6d4): every face cut at the depth TRACK_DATA gives (the image's header, 0x70: 50000, track 4
   60000, the view's z from the camera), a piece only while its position is within that + 8192
   units of the camera */
#define REACH2      reach2
#define REACH_PLUS  8192.0f
/* The original's lens: screen = centre + 128 x / (128 + z), z the depth from the camera. The
   corners' divide (0x83420) reads TABS\PERSP01.TAB (loaded by 0x4cd4c, its address patched in
   by 0x8ba28): entry z = -65536 x 128 / (128 + z); a cut at the near plane z = 0 (0xd7470)
   projects with 128 / 128 (0xd7493, ds:0x829ac = -128), one at the far plane with
   128 / (far - 128) (0xd7837, ds:0x829b0 = far - 128, 0x8bad6). So the eye is EYE_BACK
   behind the camera along the view: the near plane through the camera (depth 128 from the
   eye), the scale 128 at the eye. The camera itself stays the origin of the view's z, the
   reach, the drawing order (the places less the camera, 0x8ab8e) and the faces' row (0x834d6:
   the camera in the block's frame, from those places; face_eye). The eye line 65 % down the view, in
   whole lines (0x19cbe: 0x4c610(320, h, h x 0.65 cut by 0x4d7b4): 130 of 200, 85 of the
   cockpit's 132) */
#define NEAR        128.0f
#define FOCAL       128.0f
#define EYE_BACK    128.0f
#define EYE_LINE(h) ((h) * 65 / 100)
#define CAM_UP      700.0f      /* camera over the player's line */
#define LOOK        12          /* path points ahead for the direction of view */
#define PI          3.14159265f

/* The game's view angles (0x829e3): each cut to a 2048th of a turn (the camera record's float,
   0 .. 2 pi, times 2048 / 2 pi, cut towards 0) and its sine and cosine from the table at
   0x30190 (16 bits, 32768 = 1, halved: 16384 = 1); the table is sin x 32768 cut towards 0
   (32 of its 2048 entries are 1 larger) */
static int view_angle(float a)
{
    a = fmodf(a, 2.0f * PI);
    if(a < 0.0f) a += 2.0f * PI;
    return (int)(a * (2048.0f / (2.0f * PI))) & 2047;
}

static int32_t view_sin16(int i)          /* 16384 = 1 */
{
    return (int32_t)(sinf((float)i * (PI / 1024.0f)) * 32768.0f) >> 1;
}

struct piece { int32_t obj, x, y, h, t; };
struct pt { int32_t x, y, h; };
struct start { int32_t x, y; float yaw; };

static const uint8_t *file_start, *mlb, *unr_blob;
static uint32_t file_size;
static int nblocks;
static const int32_t *unr_off;
static int unr_count;
static const struct piece *pieces;
static int npieces;
static const struct pt *path;
static int npath;
static const uint32_t *pal;
static const uint16_t *ground_first, *ground_list;     /* first[65537], then the pieces */
static const struct track_marker *markers;
static int nmarkers;
static const struct car_spec *cars;
static int ncars;
static const struct track_model *models;
static int nmodels;
static const struct start *start;
static const uint8_t *models_at;
static const struct track_dir *dirs;
static int ndirs;
static const struct track_wall *walls;
static int nwalls;
static const uint8_t *piece_kinds;     /* the road kind of each placement (header 0x74), NULL if the image has none */
static float anim;                      /* the animation clock, 0..1 */
static uint32_t piece_phase;            /* the pieces' texture phase: the clock (record +0x20 is -1) */
static uint32_t clock16;                /* the animation clock, 16 bits (0x21538) */
static uint8_t multi[MAX_OBJ];          /* the mesh has several blocks or moving parts */
static int16_t class_key[MAX_OBJ];      /* the key's change for each mesh (the header's entry 43, track_class_key) */

/* What track_under() reads, made once from the drivable meshes: each mesh's faces of its
   first face row in that order, with corners (x, z), normal, first corner, a box around them
   and whether the face is convex with an area (only then a point outside the box cannot be
   over it: a flat or bent face always gets the full test, so the answers stay the same) */
struct cface { float x0, x1, z0, z1; int16_t x[4], z[4], y[4], n[3], v[3]; uint8_t nc, boxed; };
#define MAX_CFACES  8192
static struct cface cfaces[MAX_CFACES];
static int ncfaces;
static int32_t cobj_first[MAX_OBJ];     /* -1: no table (not drivable, or the table was full) */
static int16_t cobj_count[MAX_OBJ];
static uint8_t cobj_grass[MAX_OBJ];
/* and by place: each drivable mesh's box round its boxed faces in CGRID x CGRID cells, each cell
   with the faces a point in it may be over, in their order: those whose box reaches into the
   cell and those without a box. Only faces the box test would pass over are left out, so a
   point finds the same face as through all of them. at[0] -1: no grid (the list was full) */
#define CGRID       8
#define MAX_CLIST   65536
static struct cgrid { float x0, z0, sx, sz; int32_t at[CGRID * CGRID + 1]; } cgrid[MAX_OBJ];
static uint16_t clist[MAX_CLIST];       /* the cells' faces, from the mesh's first */
/* the ground list's placements as track_under reads them, worked out once (the same values):
   place, its block's middle, the square of its reach, its turn; obj -1 if it has no faces */
struct gpiece { float x, y, ox, oy, r2, c, s; int32_t obj, h, br; uint8_t kind; };
#define MAX_GROUND  4096
static struct gpiece gpieces[MAX_GROUND];
static float sin_q[513];                /* sin(2 pi i / 2048) for i = 0..512 */
static float under_n[3];                /* track_under's face: its normal in world axes (x, y, up) */
static float far_z = 50000.0f;          /* the far plane (the image's header, track_init) */
static float reach2 = (50000.0f + REACH_PLUS) * (50000.0f + REACH_PLUS);   /* the pieces' reach, squared */
static float radius[MAX_OBJ];           /* of each mesh around its origin */
/* the placements as the culling reads them, from the track's set-up: place as floats (the same
   values), its mesh's reach (-1: no mesh) and class key */
static struct cpiece { float x, y, h, rad; } cpieces[MAX_PIECES];
static int16_t cpiece_key[MAX_PIECES];
/* The placements in cells, as they lie in the file (a cell's placements together, the cells row by row), each
   cell's box round their places: a cell out of reach is passed over at once. The cells and the lists of cells
   that may be seen from each one come from the visibility table after the placements (tb_mmp.h,
   tb_mmp_vis(): the original's cells and their run 4); an image without it gets groups of 16 placements as
   cells and no lists (everything in reach is looked at) */
#define VIS_MAGIC   0x31534956u     /* "VIS1" */
#define MAX_CELLS   1024            /* 251 to 843 on Screamer 2's seven tracks */
#define GROUP       16
struct vcell { uint16_t key, first, count, list, nlist; };      /* the table's cell: key = row << 8 | column */
static const struct vcell *vcells;
static const uint16_t *vlist;
static int nvcells;
/* A cell's box round the places of its placements that have a mesh (x0 .. y1, the reach test), the middle of their
   heights and the radius of the sphere round box and heights (r0), the largest reach of their meshes (rmax): enough to
   tell that none of the cell's placements can be in view (cell_out) */
static struct { float x0, y0, x1, y1, ch, r0, rmax; uint16_t first, count; } cells[MAX_CELLS];
static int ncells;
static int cells_keyed;                 /* every placement lies in its cell's square of the map's grid: draw() may pick the
                                           cells of the camera's window by their keys instead of looking at all of them */

int track_mode;
void (*track_hw_piece)(const struct gu_piece *p);

#ifdef __riscv
static inline uint32_t now(void)
{
    uint32_t c;
    __asm__ volatile ("csrr %0, cycle" : "=r"(c));
    return c;
}
/* the FPU's answers to comparisons (whole numbers) all asked for before any is used: an answer
   comes back late, and a use right after its question waits for it; asked one after the other,
   they come back side by side and the CPU waits once */
#define INT_NOW2(a, b) __asm__ ("" : "+r"(a), "+r"(b))
#define INT_NOW4(a, b, c, d) __asm__ ("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d))
#define INT_NOW6(a, b, c, d, e, f) __asm__ ("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f))
#define INT_NOW7(a, b, c, d, e, f, g) \
    __asm__ ("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f), "+r"(g))
#define INT_NOW8(a, b, c, d, e, f, g, h) \
    __asm__ ("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f), "+r"(g), "+r"(h))
#else
static inline uint32_t now(void) { return 0; }
#define INT_NOW2(a, b) ((void)0)
#define INT_NOW4(a, b, c, d) ((void)0)
#define INT_NOW6(a, b, c, d, e, f) ((void)0)
#define INT_NOW7(a, b, c, d, e, f, g) ((void)0)
#define INT_NOW8(a, b, c, d, e, f, g, h) ((void)0)
#endif

static int32_t rd32(const uint8_t *p) { return *(const int32_t *)p; }
static int16_t rd16(const uint8_t *p) { return *(const int16_t *)p; }

static float sin2048(uint32_t a)
{
    a &= 2047;
    float s = sin_q[a & 512 ? 512 - (a & 511) : a & 511];
    return a & 1024 ? -s : s;
}

static const uint8_t *block_of(int k, const uint8_t **obj)
{
    *obj = unr_blob + unr_off[k];
    return *obj + rd32(*obj + 0x18);
}

/* the cell of v along an axis of a grid: never smaller for a larger v, so a face's box from lo
   to hi reaches into the cells cell(lo) .. cell(hi), and a point between lies in one of those.
   (v - v0) * s stays well inside an int: the boxes are whole numbers at least 1 apart, s at
   most CGRID / 1, and track_under asks only for places nearer than 4,000,000 */
static inline int cgrid_clamp(int i)
{
    return i < 0 ? 0 : i > CGRID - 1 ? CGRID - 1 : i;
}

static inline int cgrid_cell(float v, float v0, float s)
{
    return cgrid_clamp((int)((v - v0) * s));
}

/* the cells a face reaches into: all for one without a box */
static void cgrid_cells(const struct cgrid *g, const struct cface *q, int r[4])
{
    if(!q->boxed) {
        r[0] = r[2] = 0;
        r[1] = r[3] = CGRID - 1;
        return;
    }
    r[0] = cgrid_cell(q->x0, g->x0, g->sx);
    r[1] = cgrid_cell(q->x1, g->x0, g->sx);
    r[2] = cgrid_cell(q->z0, g->z0, g->sz);
    r[3] = cgrid_cell(q->z1, g->z0, g->sz);
}

static void cgrid_build(void)
{
    int n = 0;
    for(int k = 0; k < unr_count; k++) {
        struct cgrid *g = &cgrid[k];
        g->at[0] = -1;
        if(cobj_first[k] < 0) continue;
        const struct cface *f0 = &cfaces[cobj_first[k]];
        int nf = cobj_count[k], any = 0, r[4], cnt[CGRID * CGRID] = { 0 }, total = 0;
        float x0 = 0.0f, x1 = 0.0f, z0 = 0.0f, z1 = 0.0f;
        for(int j = 0; j < nf; j++) {
            const struct cface *q = &f0[j];
            if(!q->boxed) continue;
            if(!any || q->x0 < x0) x0 = q->x0;
            if(!any || q->x1 > x1) x1 = q->x1;
            if(!any || q->z0 < z0) z0 = q->z0;
            if(!any || q->z1 > z1) z1 = q->z1;
            any = 1;
        }
        g->x0 = x0;
        g->z0 = z0;
        g->sx = x1 > x0 ? (float)CGRID / (x1 - x0) : 0.0f;
        g->sz = z1 > z0 ? (float)CGRID / (z1 - z0) : 0.0f;
        for(int j = 0; j < nf; j++) {                   /* a face without corners is never found */
            if(!f0[j].nc) continue;
            cgrid_cells(g, &f0[j], r);
            for(int cz = r[2]; cz <= r[3]; cz++)
                for(int cx = r[0]; cx <= r[1]; cx++) cnt[cz * CGRID + cx]++;
            total += (r[1] - r[0] + 1) * (r[3] - r[2] + 1);
        }
        if(n + total > MAX_CLIST) continue;
        int at[CGRID * CGRID];
        for(int c = 0; c < CGRID * CGRID; c++) {
            g->at[c] = at[c] = n;
            n += cnt[c];
        }
        g->at[CGRID * CGRID] = n;
        for(int j = 0; j < nf; j++) {                   /* in the faces' order */
            if(!f0[j].nc) continue;
            cgrid_cells(g, &f0[j], r);
            for(int cz = r[2]; cz <= r[3]; cz++)
                for(int cx = r[0]; cx <= r[1]; cx++) clist[at[cz * CGRID + cx]++] = (uint16_t)j;
        }
    }
}

/* ground list entry at as track_under reads it */
static void gpiece_make(int at, struct gpiece *g)
{
    const struct piece *pc = &pieces[ground_list[at]];
    g->obj = -1;
    if(pc->obj < 0 || pc->obj >= unr_count || cobj_first[pc->obj] < 0) return;
    const uint8_t *obj, *blk = block_of(pc->obj, &obj);
    float r = (float)rd32(blk);
    g->x = (float)pc->x;
    g->y = (float)pc->y;
    g->ox = (float)rd16(blk + 4);
    g->oy = (float)rd16(blk + 8);
    g->br = rd32(blk);
    g->kind = piece_kinds ? piece_kinds[ground_list[at]] : 0;
    g->r2 = r * r;
    g->c = sin2048((uint32_t)pc->t + 512);
    g->s = sin2048((uint32_t)pc->t);
    g->obj = pc->obj;
    g->h = pc->h;
}

int track_open(const volatile uint8_t *file)
{
    const uint8_t *f = (const uint8_t *)file;
    if(rd32(f) != 0x4B524353 || rd32(f + 4) != 7) return 0;    /* "SCRK", version 7 (Screamer 2) */
    file_start = f;
    file_size = (uint32_t)rd32(f + 0x88);
    nblocks = rd32(f + 0x8C);
    if(nblocks < 1 || 0x90 + 4 * nblocks > 0x100) return 0;
    return 1;
}

int track_init(void)
{
    const uint8_t *f = file_start;
    mlb = f + rd32(f + 0x08);
    const uint8_t *unr = f + rd32(f + 0x10);
    pal = (const uint32_t *)(f + rd32(f + 0x18));
    pieces = (const struct piece *)(f + rd32(f + 0x1C));
    npieces = rd32(f + 0x20);
    path = (const struct pt *)(f + rd32(f + 0x24));
    npath = rd32(f + 0x28);
    ground_first = (const uint16_t *)(f + rd32(f + 0x2C));
    ground_list = ground_first + 65537;
    markers = (const struct track_marker *)(f + rd32(f + 0x34));
    nmarkers = rd32(f + 0x38);
    cars = (const struct car_spec *)(f + rd32(f + 0x3C));
    ncars = rd32(f + 0x40);
    models_at = f + rd32(f + 0x44);
    models = (const struct track_model *)models_at;
    nmodels = rd32(f + 0x48);
    dirs = (const struct track_dir *)(f + rd32(f + 0x4C));
    ndirs = rd32(f + 0x50);
    walls = (const struct track_wall *)(f + rd32(f + 0x54));
    nwalls = rd32(f + 0x58);
    start = (const struct start *)(f + 0x5C);
    piece_kinds = rd32(f + 0x74) ? f + rd32(f + 0x74) : NULL;
    unr_count = rd32(unr + 4);
    if(unr_count > MAX_OBJ || npieces > MAX_PIECES || ncars < 0 || nmodels < 0) return 0;     /* (a menu's room has no cars: only a damaged count is negative) */
    int32_t far = rd32(f + 0x70);
    far_z = far > 0 ? (float)far : 50000.0f;
    reach2 = (far_z + REACH_PLUS) * (far_z + REACH_PLUS);
    unr_off = (const int32_t *)(unr + 8);
    unr_blob = unr + 8 + 4 * unr_count;

    const float dc = 0.99999529380957617f, ds = 0.0030679567629659761f;  /* cos, sin of 2 pi / 2048 */
    float c = 1.0f, s = 0.0f;
    for(int i = 0; i <= 512; i++) {
        sin_q[i] = s;
        float t = c * dc - s * ds;
        s = s * dc + c * ds;
        c = t;
    }

    ncfaces = 0;
    for(int k = 0; k < unr_count; k++) cobj_first[k] = -1;
    for(int i = 0; i < ground_first[65536]; i++) {
        int k = pieces[ground_list[i]].obj;
        if(k < 0 || k >= unr_count || cobj_first[k] >= 0) continue;
        const uint8_t *obj, *blk = block_of(k, &obj), *verts = obj + rd32(blk + 0x1C);
        int nface = rd32(blk + 0x14), ro = rd32(blk + 0x28), first = ncfaces;
        if(ro <= 0 || ncfaces + nface > MAX_CFACES) continue;
        for(int j = 0; j < nface; j++) {
            uint16_t e = *(const uint16_t *)(blk + ro + 2 * j);
            if(e == 0xFFFF) continue;                   /* an end mark, no face */
            const uint8_t *fc = blk + e;
            struct cface *q = &cfaces[ncfaces++];
            int nc = rd16(fc + 6);
            q->nc = (uint8_t)(nc < 3 ? 3 : nc > 4 ? 4 : nc);
            for(int c = 0; c < q->nc; c++) {
                int vo = rd16(fc + 8 + 2 * c);
                if(vo < 0) { q->nc = 0; break; }       /* a corner that is none: never over it */
                q->x[c] = rd16(verts + vo);
                q->z[c] = rd16(verts + vo + 4);
                q->y[c] = rd16(verts + vo + 2);
                if(c == 0) q->v[0] = q->x[0], q->v[1] = rd16(verts + vo + 2), q->v[2] = q->z[0];
            }
            for(int c = 0; c < 3; c++) q->n[c] = rd16(fc + 0x14 + 4 * c);
            q->boxed = 0;
            if(!q->nc) continue;
            int64_t s = 0;
            int pos = 0, neg = 0;
            q->x0 = q->x1 = q->x[0];
            q->z0 = q->z1 = q->z[0];
            for(int c = 0; c < q->nc; c++) {
                int a = c, b = (c + 1) % q->nc, d = (c + 2) % q->nc;
                int64_t t = (int64_t)(q->x[b] - q->x[a]) * (q->z[d] - q->z[b]) - (int64_t)(q->z[b] - q->z[a]) * (q->x[d] - q->x[b]);
                pos += t > 0; neg += t < 0;
                s += (int64_t)q->x[a] * q->z[b] - (int64_t)q->x[b] * q->z[a];
                if(q->x[c] < q->x0) q->x0 = q->x[c];
                if(q->x[c] > q->x1) q->x1 = q->x[c];
                if(q->z[c] < q->z0) q->z0 = q->z[c];
                if(q->z[c] > q->z1) q->z1 = q->z[c];
            }
            q->boxed = s != 0 && !(pos && neg) && pos + neg == q->nc;
        }
        cobj_first[k] = first;
        cobj_count[k] = (int16_t)(ncfaces - first);
        cobj_grass[k] = blk[0x6b] == 'e';
    }
    cgrid_build();
    for(int at = 0; at < ground_first[65536] && at < MAX_GROUND; at++) gpiece_make(at, &gpieces[at]);

    for(int k = 0; k < unr_count; k++) {
        const uint8_t *obj, *blk = block_of(k, &obj);
        int nb = rd32(obj + 0x14), frames = rd32(obj + 0x10);
        multi[k] = nb > 1 || frames > 0;
        /* the reach round the origin: each block's vertices, and how far its place or frames
           move it (mesh_reach) */
        radius[k] = 0.0f;
        for(int b = 0; b < (multi[k] ? nb : 1) && b < MAX_BLOCKS; b++) {
            blk = obj + rd32(obj + 0x18 + 4 * b);
            const uint8_t *v = obj + rd32(blk + 0x1C);
            int n = rd32(blk + 0x10);
            int32_t so = rd32(blk + 0x0C);
            float r2 = 0.0f, t2 = 0.0f;
            for(int i = 0; i < n && i < GU_MAX_VERT; i++, v += 6) {
                float x = rd16(v), y = rd16(v + 2), z = rd16(v + 4), d = x * x + y * y + z * z;
                if(d > r2) r2 = d;
            }
            if(multi[k] && so > 0)
                for(int fr = 0; fr < (frames > 0 ? frames : 1); fr++) {
                    float tx, ty, tz;
                    if(frames > 0) {
                        const uint8_t *q = obj + so + 0x1C + 0x30 * fr;
                        tx = rd16(q + 18); ty = rd16(q + 20); tz = rd16(q + 22);
                    } else {
                        tx = (float)rd32(obj + so + 0x10); ty = (float)rd32(obj + so + 0x14); tz = (float)rd32(obj + so + 0x18);
                    }
                    if(tx * tx + ty * ty + tz * tz > t2) t2 = tx * tx + ty * ty + tz * tz;
                }
            float rb = __builtin_sqrtf(r2) + __builtin_sqrtf(t2);
            if(rb > radius[k]) radius[k] = rb;
        }
        class_key[k] = (int16_t)track_class_key(obj);
    }
    for(int i = 0; i < npieces; i++) {
        int k = pieces[i].obj, ok = k >= 0 && k < unr_count;
        cpieces[i] = (struct cpiece){ (float)pieces[i].x, (float)pieces[i].y, (float)pieces[i].h, ok ? radius[k] : -1.0f };
        cpiece_key[i] = ok ? class_key[k] : 0;
    }
    /* the cells: the table's, if the image has one and it fits, else groups of 16 placements */
    const uint32_t *vh = (const uint32_t *)(pieces + npieces);
    nvcells = vh[0] == VIS_MAGIC && vh[1] > 0 && vh[1] <= MAX_CELLS ? (int)vh[1] : 0;
#ifdef TRACK_NO_VIS                             /* the PC tools' NOVIS=1: as before the lists (everything in reach is looked at) */
    nvcells = 0;
#endif
    vcells = (const struct vcell *)(vh + 3);
    vlist = (const uint16_t *)(vcells + nvcells);
    ncells = nvcells ? nvcells : (npieces + GROUP - 1) / GROUP;
    cells_keyed = nvcells > 0;
    for(int g = 0; g < ncells; g++) {
        int first = nvcells ? vcells[g].first : g * GROUP;
        int n = nvcells ? vcells[g].count : npieces - first < GROUP ? npieces - first : GROUP;
        if(first + n > npieces) return 0;
        cells[g].first = (uint16_t)first;
        int m = 0;                                  /* the placements with a mesh: the others are never drawn */
        float h0 = 0.0f, h1 = 0.0f, rmax = 0.0f;
        for(int i = first; i < first + n; i++) {
            if(cpieces[i].rad < 0.0f) continue;
            if(nvcells) {                           /* the map's cell of its place (camera_cell) must be this cell */
                uint32_t key = (uint32_t)(((int32_t)cpieces[i].y + 0x100000) >> 13) << 8 | (uint32_t)(((int32_t)cpieces[i].x + 0x100000) >> 13);
                if(key != vcells[g].key) cells_keyed = 0;
            }
            if(!m++) {
                cells[g].x0 = cells[g].x1 = cpieces[i].x;
                cells[g].y0 = cells[g].y1 = cpieces[i].y;
                h0 = h1 = cpieces[i].h;
            }
            if(cpieces[i].x < cells[g].x0) cells[g].x0 = cpieces[i].x;
            if(cpieces[i].x > cells[g].x1) cells[g].x1 = cpieces[i].x;
            if(cpieces[i].y < cells[g].y0) cells[g].y0 = cpieces[i].y;
            if(cpieces[i].y > cells[g].y1) cells[g].y1 = cpieces[i].y;
            if(cpieces[i].h < h0) h0 = cpieces[i].h;
            if(cpieces[i].h > h1) h1 = cpieces[i].h;
            if(cpieces[i].rad > rmax) rmax = cpieces[i].rad;
        }
        cells[g].count = (uint16_t)(m ? n : 0);     /* (a cell with nothing to draw is passed over) */
        if(!m) cells[g].x0 = cells[g].x1 = cells[g].y0 = cells[g].y1 = 0.0f;
        float ex = 0.5f * (cells[g].x1 - cells[g].x0), ey = 0.5f * (cells[g].y1 - cells[g].y0), eh = 0.5f * (h1 - h0);
        cells[g].ch = 0.5f * (h0 + h1);
        cells[g].r0 = __builtin_sqrtf(ex * ex + ey * ey + eh * eh);
        cells[g].rmax = rmax;
    }
    return npieces;
}

int track_class_key(const uint8_t *obj)
{
    /* the object header's table entry 43 (+0xC4), read at 0x7e9a5 and taken off the 16-bit key */
    return obj ? -(int)(int16_t)rd32(obj + 0xC4) : 0;
}

/* The rear-view mirror's pass (track_draw_mirror): the picture is the glass's, turned over (the camera's right-hand side reversed: the geometry unit's test of a face is the eye's side
   of its plane, which no reflection changes, and the rasterizer takes either way round on screen), the backdrop turned too, and the level of detail is at least 1 (STARTL.EXE 0x756b0
   sets [0xae6b4], which 0x7f0fc and 0x7f214 read) */
static int mirror_pass;

/* How far the mirror's pass sees, in percent of the track's far plane: the glass is 160 x 35 pixels, and the pass hands over pieces as the main pass does. Measured in the simulation of
   track 4 (docs/screamer2/hud.md section 5), the mirror's cost: the whole far plane 7.8 ms (the race 20.6 ms a frame, 198 of 220 late), 70 % no different to look at, 50 % 4.3 ms (18.0 ms,
   8 late; the bridges' decks stay, a few far houses at the side go), 35 % 3.5 ms but the bridges after the tunnel were gone (the owner saw it); the race without the mirror takes 11.8 ms
   (13.5 in INCAR). Leaving out what is too small to be seen instead saved nothing (7.6 ms): the pieces far behind the car are big blocks of the city */
#ifndef MIRROR_FAR_PCT
#define MIRROR_FAR_PCT  50
#endif

/* The original's level of detail per block (STARTL.EXE 0x7f148 for placed meshes, 0x7f010 for cars and
   other moving objects; docs/screamer2/performance.md, section 6): the distance of the block's middle
   from the camera, rounded to nearest (even). Placed: key = -(dist >> 1), level 0 above -3000 (dist <
   6000), level 1 above -4500 (dist < 9000), else 2. Moving: key = -dist, -2000 and -6000. These are
   constants of the game's image, no option. The mirror's pass (at least level 1) is not in this core
   yet. -DTRACK_NO_LOD (the PC renderer, tools/s2/render_s2.sh) draws every block at level 0. */
int track_lod_level(float dist, int moving)
{
#ifdef TRACK_NO_LOD
    (void)dist;
    (void)moving;
    return 0;
#else
    int32_t d = (int32_t)lrintf(dist);
    int32_t key = moving ? -d : -(d >> 1);
    const int level = key > (moving ? -2000 : -3000) ? 0 : key > (moving ? -6000 : -4500) ? 1 : 2;
    return level < mirror_pass ? mirror_pass : level;
#endif
}

float track_far(void)
{
    return far_z;
}

const uint32_t *track_palette(void)
{
    return pal;
}

const uint8_t *track_mlb(void)
{
    return mlb;
}

/* the effects' block at the end of the MLB section (smg.h): its directory is the section's last 256 bytes, "S2FX", 1, the table, the three meshes, the rows used, the texels found */
int track_fx(struct track_fx *fx)
{
    uint32_t size = (uint32_t)rd32(file_start + 0x0C);
    if(!mlb || size < 256 + 16384 || rd32(mlb + size - 256) != 0x58463253 || rd32(mlb + size - 252) != 1) return 0;
    const uint8_t *d = mlb + size - 256;
    fx->table = mlb + rd32(d + 8);
    fx->shadow = rd32(d + 12) ? mlb + rd32(d + 12) : 0;
    fx->smoke = rd32(d + 16) ? mlb + rd32(d + 16) : 0;
    fx->smoke2 = rd32(d + 20) ? mlb + rd32(d + 20) : 0;
    fx->rows = rd32(d + 24);
    return 1;
}

uint32_t track_path_length(void)
{
    return (uint32_t)npath;
}

void track_path_point(uint32_t i, float *x, float *y, float *h)
{
    const struct pt *p = &path[i % (uint32_t)npath];
    *x = (float)p->x;
    *y = (float)p->y;
    *h = (float)p->h;
}

const struct track_marker *track_markers(int *n)
{
    *n = nmarkers;
    return markers;
}

const struct car_spec *track_cars(int *n)
{
    *n = ncars;
    return cars;
}

const struct track_model *track_models(int *n)
{
    *n = nmodels;
    return models;
}

const uint8_t *track_model_mesh(int k)
{
    return models_at + models[k].mesh;
}

const struct track_dir *track_dirs(int *n)
{
    *n = ndirs;
    return dirs;
}

const struct track_wall *track_walls(int *n)
{
    *n = nwalls;
    return walls;
}

uint32_t track_phase(void)
{
    return piece_phase;
}

void track_clock(uint32_t steps)
{
    anim += (float)steps * 0.02f * 0.3f;
    while(anim >= 1.0f) anim -= 1.0f;
    clock16 = (uint32_t)(anim * 65535.0f);
    piece_phase = clock16 & 0x3FFF;
}

int track_bonus_time(void)
{
    return rd32(file_start + 0x70);
}

int track_camera_hi(void)
{
    return rd32(file_start + 0x74);
}

const uint8_t *track_backdrop(void)
{
    return file_start + rd32(file_start + 0x78);
}

int track_ground_colour(void)
{
    return rd32(file_start + 0x7C) & 255;
}

/*
 * The backdrop as the game copies it (0x83a68): the picture's column (2047 - heading) / 2
 * mod 512 under the view's left edge (heading in the game's units, 2048 a turn, = yaw + a
 * quarter turn: twice round the picture a turn), its top row at the eye line + 22 - 256 +
 * 128 sin(pitch), taken from row |top| on (a top below the view's top in every race), cut at
 * the view's bottom; each row read on past its end as the game does; the rest of the view
 * in the ground colour. Upright whatever the camera's roll: the game's leaning version
 * (0x83b44) is never called.
 */
/* The backdrop's panorama with every row read from the other end (the rear-view mirror's pass), made when it is first asked for with this track's panorama: 128 KB, one pass over it */
static uint8_t *backdrop_turned(const uint8_t *pano)
{
    static uint8_t turned[512 * 256] __attribute__((aligned(4)));
    static const uint8_t *made_for;
    if(made_for != pano) {
        for(int y = 0; y < 256; y++)
            for(int x = 0; x < 512; x++) turned[y * 512 + x] = pano[y * 512 + 511 - x];
        made_for = pano;
    }
    return turned;
}

static void backdrop_blit(struct blit_list *l, const struct rs_clip *clip, const struct track_cam *cam, int mirror)
{
    int w = clip->x1 - clip->x0, h = clip->y1 - clip->y0, eye = EYE_LINE(h);
    int32_t s = view_sin16(view_angle(cam->pitch));             /* the game's sine, [0x8228c] */
    int top = eye + 22 - 256 + ((-54152 * s) >> 7) / -54152;
    int heading = (int)lrintf((cam->yaw + 1.5707964f) * (1024.0f / 3.14159265f));
    int col = ((2047 - heading) >> 1) & 511;
    const uint8_t *pano = track_backdrop();
    if(mirror) {                                                /* the glass shows the picture turned over, the panorama's rows read from the other end: column x of the window is column 319 - x of the ordinary one */
        pano = backdrop_turned(pano);
        col = (192 - col) & 511;
    }
    int rows = 256 + (top < 0 ? top : 0);
    if(top + 256 > h) rows -= top + 256 - h;
    if(rows > 0)
        blit_copy(l, pano + (top < 0 ? -top : top) * 512 + col, w, rows, 512, clip->x0, clip->y0,
                  BLIT_COPY | BLIT_OPAQUE, 0);
    else
        rows = 0;
    if(rows < h) blit_fill(l, clip->x0, clip->y0 + rows, w, h - rows, track_ground_colour());
}

void track_backdrop_blit(struct blit_list *l, const struct rs_clip *clip, const struct track_cam *cam)
{
    backdrop_blit(l, clip, cam, 0);
}

void track_backdrop_blit_mirror(struct blit_list *l, const struct rs_clip *clip, const struct track_cam *cam)
{
    backdrop_blit(l, clip, cam, 1);
}

const uint8_t *track_hud(void)
{
    return file_start + rd32(file_start + 0x68);
}

const struct blit_image *track_hud_images(int *n)
{
    const uint8_t *h = track_hud();
    *n = rd32(h);
    return (const struct blit_image *)(h + 8);
}

const struct blit_font *track_hud_fonts(int *n)
{
    const uint8_t *h = track_hud();
    *n = rd32(h + 4);
    return (const struct blit_font *)(h + 8 + 12 * rd32(h));
}

void track_hud_picture(int k, int *first, int *count)
{
    const uint8_t *h = track_hud(), *p = h + 8 + 12 * rd32(h) + 20 * rd32(h + 4) + 8 * k;
    *first = rd32(p);
    *count = rd32(p + 4);
}

const char *track_hud_text(int k)
{
    const uint8_t *h = track_hud();
    return (const char *)(h + 8 + 12 * rd32(h) + 20 * rd32(h + 4) + 16 + 32 * k);
}

void track_start(float *x, float *y, float *yaw)
{
    *x = (float)start->x;
    *y = (float)start->y;
    *yaw = start->yaw;
}

/* track_under's test of face q of placement gp at (lx, lz) in its frame (c, s its turn): 1 the
   point is over it (*h, under_n set if a height is asked for), 0 the next face */
static inline int under_face(const struct cface *q, const struct gpiece *gp, float lx, float lz, float c, float s,
                             float *h, int *grass)
{
    /* (each test's comparisons all asked for before any answer is used: they come back side by
       side, and the CPU waits once) */
    if(q->boxed) {
        int w = lx < q->x0, e = lx > q->x1, s0 = lz < q->z0, n = lz > q->z1;
        INT_NOW4(w, e, s0, n);
        if(w | e | s0 | n) return 0;
    }
    int nc = q->nc;
    if(!nc) return 0;
    /* the point's side of each edge, corner j to the next; a triangle's fourth (corner 3 is
       none) is worked out but not counted */
#define EDGE(j, b) ((q->x[b] - (float)q->x[j]) * (lz - (float)q->z[j]) - (q->z[b] - (float)q->z[j]) * (lx - (float)q->x[j]))
    int four = nc > 3;
    float cr0 = EDGE(0, 1), cr1 = EDGE(1, 2), cr2 = EDGE(2, four ? 3 : 0), cr3 = EDGE(3, 0);
#undef EDGE
    int p0 = cr0 > 0.0f, p1 = cr1 > 0.0f, p2 = cr2 > 0.0f, p3 = cr3 > 0.0f;
    int n0 = cr0 < 0.0f, n1 = cr1 < 0.0f, n2 = cr2 < 0.0f, n3 = cr3 < 0.0f;
    INT_NOW8(p0, p1, p2, p3, n0, n1, n2, n3);
    int pos = p0 + p1 + p2 + (four & p3), neg = n0 + n1 + n2 + (four & n3);
    if(pos && neg) return 0;
    if(grass) *grass = cobj_grass[gp->obj];
    if(!h) return 1;
    if(q->n[1] == 0) return 0;
    int num = ((int)lx - q->v[0]) * q->n[0] + ((int)lz - q->v[2]) * q->n[2];
    *h = (float)gp->h + (float)q->v[1] + (float)(-num / q->n[1]);
    under_n[0] = (float)q->n[0] * c - (float)q->n[2] * s;
    under_n[1] = (float)q->n[0] * s + (float)q->n[2] * c;
    under_n[2] = (float)q->n[1];
    return 1;
}

int track_under(float x, float y, float *h, int *grass)
{
    static const int8_t cell[9][2] = { {0,0}, {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };
    int cx = ((int)x + 0x100000) >> 13, cy = ((int)y + 0x100000) >> 13;
    int near = (fabsf(x) < 4.0e6f) & (fabsf(y) < 4.0e6f);  /* (else, or not a number: all faces) */
    for(int ci = 0; ci < 9; ci++) {
        int kx = cx + cell[ci][0], ky = cy + cell[ci][1];
        if(kx < 0 || kx > 255 || ky < 0 || ky > 255) continue;
        for(int at = ground_first[ky * 256 + kx]; at < ground_first[ky * 256 + kx + 1]; at++) {
            struct gpiece made;
            const struct gpiece *gp = at < MAX_GROUND ? &gpieces[at] : (gpiece_make(at, &made), &made);
            if(gp->obj < 0) continue;
            float dx = x - gp->x, dy = y - gp->y;
            float bx = dx - gp->ox, by = dy - gp->oy;
            if(bx * bx + by * by > gp->r2) continue;
            float c = gp->c, s = gp->s;
            float lx = dx * c + dy * s, lz = -dx * s + dy * c;  /* into the piece's frame */
            const struct cface *f0 = &cfaces[cobj_first[gp->obj]], *fe = f0 + cobj_count[gp->obj];
            const struct cgrid *g = &cgrid[gp->obj];
            for(int tries = 0; tries < 3; tries++, lx += 10.0f, lz += 10.0f)
                if(g->at[0] >= 0 && near) {
                    int iz = (int)((lz - g->z0) * g->sz), ix = (int)((lx - g->x0) * g->sx);
                    INT_NOW2(iz, ix);                   /* (cgrid_cell's, both asked for at once) */
                    int k = cgrid_clamp(iz) * CGRID + cgrid_clamp(ix);
                    for(const uint16_t *li = clist + g->at[k], *le = clist + g->at[k + 1]; li < le; li++)
                        if(under_face(f0 + *li, gp, lx, lz, c, s, h, grass)) return 1;
                } else
                    for(const struct cface *q = f0; q < fe; q++)
                        if(under_face(q, gp, lx, lz, c, s, h, grass)) return 1;
        }
    }
    return 0;
}

/* Screamer 2's own "what is under this point" (0x28ce6 -> 0x28db7 -> 0x4c24c, OpenScreamer's s2p_ground), in
   whole numbers as the game does it: the nine cells round the point in the order of the table at 0x112e84, each
   cell's drivable pieces in file order, a piece only while the point is inside its first block's circle; the
   point turned into the piece's frame (cut to whole numbers) and tried up to five times, moved 4 along both axes
   each time; the faces in the order of the block's first face row; a face holds the point when it is inside the
   box of its corners and on the inner side (>= 0) of every edge of a TRIANGLE of it -- corners 0 1 2, or for a
   quad else 0 2 3 -- one winding only. The plane is that triangle's (n = A x B with A = corner 0 less the second,
   B = corner 0 less the third; a triangle on edge, n.height 0, is passed over); the height is the second
   corner's less ((point - it) . n) / n.height, the division cut off. *held: set when any face holds the
   point, a triangle on edge too (the walls' test: the game asks the same question without the plane); the answer
   is found only with a plane. *normal is the plane's n turned back into the
   world, as it is (not made to point up); *kind the placement's road kind. 1 if found. Integer products wrap
   as the game's 32 bit ones. */
static inline int32_t imul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

static inline int s2_edge(const struct cface *q, int a, int b, int px, int pz)
{
    return imul((int)q->x[a] - q->x[b], pz - q->z[a]) + imul((int)q->z[b] - q->z[a], px - q->x[a]);
}

int track_ground_s2(int x, int y, int *held, int *height, int normal[3], int *kind)
{
    *held = 0;
    static const int8_t cell[9][2] = { {0,0}, {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };
    int cx = (x + 0x100000) >> 13, cy = (y + 0x100000) >> 13;
    for(int ci = 0; ci < 9; ci++) {
        int kx = cx + cell[ci][0], ky = cy + cell[ci][1];
        if(kx < 0 || kx > 255 || ky < 0 || ky > 255) continue;
        for(int at = ground_first[ky * 256 + kx]; at < ground_first[ky * 256 + kx + 1]; at++) {
            struct gpiece made;
            const struct gpiece *gp = at < MAX_GROUND ? &gpieces[at] : (gpiece_make(at, &made), &made);
            if(gp->obj < 0) continue;
            int dx = x - (int)gp->x, dz = y - (int)gp->y;
            int bx = dx - (int)gp->ox, bz = dz - (int)gp->oy;
            if(imul(bz, bz) + imul(bx, bx) > imul(gp->br, gp->br)) continue;
            float c = gp->c, s = gp->s;
            int lx = (int)((float)dx * c + (float)dz * s), lz = (int)((float)dz * c - (float)dx * s);
            const struct cface *f0 = &cfaces[cobj_first[gp->obj]], *fe = f0 + cobj_count[gp->obj];
            const struct cgrid *g = &cgrid[gp->obj];
            for(int tries = 0; tries < 5; tries++, lx += 4, lz += 4) {
                const uint16_t *li = 0;
                int n = (int)(fe - f0);
                if(g->at[0] >= 0) {                     /* only the faces that may hold the point, in order */
                    int iz = (int)(((float)lz - g->z0) * g->sz), ix = (int)(((float)lx - g->x0) * g->sx);
                    int k = cgrid_clamp(iz) * CGRID + cgrid_clamp(ix);
                    li = clist + g->at[k];
                    n = g->at[k + 1] - g->at[k];
                }
                for(int j = 0; j < n; j++) {
                    const struct cface *q = li ? f0 + li[j] : f0 + j;
                    if(!q->nc) continue;
                    if((float)lx > q->x1 || (float)lx < q->x0 || (float)lz > q->z1 || (float)lz < q->z0) continue;
                    int which = 0;
                    if(s2_edge(q, 0, 1, lx, lz) >= 0 && s2_edge(q, 1, 2, lx, lz) >= 0 && s2_edge(q, 2, 0, lx, lz) >= 0) which = 1;
                    else if(q->nc == 4 && s2_edge(q, 0, 2, lx, lz) >= 0 && s2_edge(q, 2, 3, lx, lz) >= 0 &&
                            s2_edge(q, 3, 0, lx, lz) >= 0) which = 2;
                    if(!which) continue;
                    if(!*held) { *held = 1; *kind = gp->kind; }         /* the first face that holds the point answers the walls' test */
                    int a = which, b = which + 1;
                    int ax = q->x[0] - q->x[a], ay = q->y[0] - q->y[a], az = q->z[0] - q->z[a];
                    int qx = q->x[0] - q->x[b], qy = q->y[0] - q->y[b], qz = q->z[0] - q->z[b];
                    int nz = imul(ax, qy) - imul(ay, qx), ny = imul(qx, az) - imul(ax, qz), nx = imul(ay, qz) - imul(az, qy);
                    if(ny == 0) continue;
                    int num = imul(lx - q->x[a], nx) + imul(lz - q->z[a], nz);
                    *height = (-num) / ny + q->y[a] + gp->h;
                    normal[0] = (int)((float)nx * c - (float)nz * s);
                    normal[1] = ny;
                    normal[2] = (int)((float)nz * c + (float)nx * s);
                    *kind = gp->kind;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* how the game stands a thing heading yaw on the face track_under found (0x1cb9c, an
   opponent): the face's normal turned into its frame, each level part x pi / 46080 of the
   normal's 16384 units an angle (1.117 x the slope); pitch + nose up, roll + right side up */
void track_under_tilt(float fx, float fy, float *pitch, float *roll)
{
    float nx = under_n[0], ny = under_n[1], nz = under_n[2];
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if(nz < 0.0f) len = -len;
    if(len != 0.0f) { nx /= len; ny /= len; }
    *pitch = -1.117f * (nx * fx + ny * fy);
    *roll = 1.117f * (nx * -fy + ny * fx);
}

int track_opp_count(int *rows)
{
    const uint8_t *o = file_start + rd32(file_start + 0x80);
    *rows = rd32(o + 4);
    return rd32(o);
}

const struct track_opp_row *track_opp_rows(void)
{
    return (const struct track_opp_row *)(file_start + rd32(file_start + 0x80) + 8);
}

const struct track_spline_node *track_opp_spline(int k, int *start, int *n)
{
    const uint8_t *o = file_start + rd32(file_start + 0x80), *p = o + 8 + 20 * rd32(o + 4);
    for(int j = 0; j < k; j++) p += 8 + 32 * rd32(p + 4);
    *start = rd32(p);
    *n = rd32(p + 4);
    return (const struct track_spline_node *)(p + 8);
}

/* the camera's frame (track_draw_cam): forward, right, up */
static void cam_frame(const struct track_cam *cam, float f[3], float r[3], float u[3])
{
    /* the last angles' frame kept (the effects and the scene ask for the same camera's): for
       the same bits the same values, without the six sines and cosines */
    static uint32_t last[3];
    static float lf[3], lr[3], lu[3];
    static int kept;
    uint32_t a[3];
    memcpy(a, &cam->yaw, sizeof a);                     /* yaw, pitch, roll */
    if(kept && a[0] == last[0] && a[1] == last[1] && a[2] == last[2]) {
        memcpy(f, lf, sizeof lf);
        memcpy(r, lr, sizeof lr);
        memcpy(u, lu, sizeof lu);
        return;
    }
    float sy = sinf(cam->yaw), cy = cosf(cam->yaw), sp = sinf(cam->pitch), cp = cosf(cam->pitch);
    float sr = sinf(cam->roll), cr = cosf(cam->roll);
    f[0] = sy * cp; f[1] = -cy * cp; f[2] = sp;
    /* level right = forward x up, level up = right x forward, then the roll about forward */
    const float r0[3] = { -cy, -sy, 0.0f };
    const float u0[3] = { r0[1] * f[2], -r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0] };
    r[0] = r0[0] * cr + u0[0] * sr; r[1] = r0[1] * cr + u0[1] * sr; r[2] = u0[2] * sr;
    u[0] = u0[0] * cr - r0[0] * sr; u[1] = u0[1] * cr - r0[1] * sr; u[2] = u0[2] * cr;
    memcpy(last, a, sizeof a);
    memcpy(lf, f, sizeof lf);
    memcpy(lr, r, sizeof lr);
    memcpy(lu, u, sizeof lu);
    kept = 1;
}

void track_cam_axes(const struct track_cam *cam, float f[3], float r[3], float u[3])
{
    cam_frame(cam, f, r, u);
}

void track_detail(int level)
{
    (void)level;    /* Screamer 2 draws at DETAIL HIGH only: the far plane is the image's (track_init) */
}

uint32_t track_size(void)
{
    return file_size;
}

int track_blocks(void)
{
    return nblocks;
}

int track_block_ok(int k)
{
    uint32_t o = 0x100 + (uint32_t)k * BLOCK, e = o + BLOCK < file_size ? o + BLOCK : file_size;
    return o < e && crc32(file_start + o, file_start + e) == (uint32_t)rd32(file_start + 0x90 + 4 * k);
}

void track_view_mirror(const struct rs_clip *clip, struct su_view *v)
{
    track_view(clip, v);
    v->cx += 48;                                /* the game draws the pass backwards and turns the picture over about its middle (the pixel x goes to 319 - x); the rasterizer's pixel x is the lens's x + 1, so the reflected lens's middle is at 162, three pixels right of the ordinary one (measured: tools/s2/check_mirror_s2.sh) */
}

void track_view(const struct rs_clip *clip, struct su_view *v)
{
    v->cx = (clip->x0 + clip->x1) * 8 - 16; /* the centre in 1/16 pixel: column 159 on the screen,
                                               as the game's projection (0x83431) */
    v->cy = (clip->y0 + EYE_LINE(clip->y1 - clip->y0)) * 16;
    v->scale = (uint32_t)(FOCAL * 16.0f);
    v->far8 = (int32_t)((far_z + EYE_BACK) * 256.0f);  /* depths from the eye */
}

/* The cell the camera is in (the table's number of it), -1 if the map has none there or the image no table:
   the original's cell of a position is ((x + 2^20) >> 13, (y + 2^20) >> 13) of its whole-number place */
static int camera_cell(float x, float y)
{
    if(!nvcells || x < -1048576.0f || x >= 1048576.0f || y < -1048576.0f || y >= 1048576.0f) return -1;
    uint32_t key = (uint32_t)(((int32_t)y + 0x100000) >> 13) << 8 | (uint32_t)(((int32_t)x + 0x100000) >> 13);
    int lo = 0, hi = nvcells;                   /* the first cell whose key is not below ours */
    while(lo < hi) {
        int mid = (lo + hi) / 2;
        if(vcells[mid].key < key) lo = mid + 1; else hi = mid;
    }
    return lo < nvcells && vcells[lo].key == key ? lo : -1;
}

/* The first cell (its number) whose key is not below key, ncells if none: the cells lie in the order of their keys */
static int first_cell(uint32_t key)
{
    int lo = 0, hi = nvcells;
    while(lo < hi) {
        int mid = (lo + hi) / 2;
        if(vcells[mid].key < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* The drawing key of a piece or object at squared distance d2 from the camera, STARTL.EXE 0x7e8c0..0x7e97b: the walk
   stores d^2 >> 3 (0x76f46), the key is -(sqrt of it, rounded to nearest, >> 2): about the distance / 11.3, far first.
   A piece adds its mesh's class key (track_class_key: minus the object header's entry 43), an object 300 more */
static inline int32_t depth_key(float d2)
{
    return -((int32_t)lrintf(sqrtf(d2 * 0.125f)) >> 2);
}

/* the list drawn: key << 16 | i, i (the low half) a piece, or npieces + an object */
#define MAX_OBJS    TRACK_MAX_OBJS      /* objects of a frame: the four cars, their shadows and puffs (s2fx.h: 68 at most), the animated scenery (s2anim.h: 16), the obstacles and sparks (s2props.h: 124) */
static int32_t vis[MAX_PIECES + MAX_OBJS];

/* The faces of a block as the game takes them (0x834d6..0x83547): the camera in the block's
   frame (x, y, z) plus the block's middle picks the face row (0x8a598; the game scales that
   point by 1.2, imul 0x13333, which leaves the row as it is): 8 more when it is below the
   block's x-z plane, then the 45-degree sector round its y axis, on a diagonal the row next to
   the x axis. The game's normal test (0x835a0) takes the same point, but its polygon filler
   (0xd55ff: one edge walks the corners forwards, the other backwards, a span only where the
   first is right of the second, 0xd5969, 0xd59eb) draws nothing of a face whose corners run
   the other way round on screen: one turned away from the lens's eye, EYE_BACK behind the
   camera. With that point alone a car's nose showed through its front tyre on the car screen
   (AAUTO00 fauto). So the normal test here takes the lens's eye; the faces it sees and the
   point leaves out (the game draws neither) changed no pixel in the frames checked. Sets
   g->row and g->e (8 fraction bits); takes g->m (its row f: the view's forward in the
   block's frame). */
static void face_eye(const uint8_t *blk, float x, float y, float z, struct gu_piece *g)
{
    const float e[3] = { x, y, z };
    float p[3] = { x + (float)rd16(blk + 4), y + (float)rd16(blk + 6), z + (float)rd16(blk + 8) };
    for(int i = 0; i < 3; i++) g->e[i] = (int32_t)((e[i] - (float)g->m[6 + i] * (EYE_BACK / 65536.0f)) * 256.0f);
    /* (all comparisons first, without branches: the FPU answers them side by side,
       where a branch on each would wait for it) */
    int below = p[1] < 0.0f, xpos = p[0] >= 0.0f, zpos = p[2] >= 0.0f, zneg = p[2] < 0.0f;
    int c0 = p[2] > p[0], c2 = -p[2] <= p[0], c4 = -p[2] > -p[0], c6 = p[2] <= -p[0];
    int row = xpos ? (zpos ? !c0 : 2 + !c2) : (zneg ? 4 + !c4 : 6 + !c6);
    g->row = below * 8 + row;
}

/* A block placed at o with m (world from local) as a piece for the geometry unit: its place in
   the view from the eye (EYE_BACK further along forward), its faces' row from the camera c */
static void block_piece(const float c[3], const float f[3], const float r[3], const float u[3],
                        const float m[9], const float o[3], const uint8_t *blk, struct gu_piece *g)
{
    const float *basis[3] = { r, u, f };
#pragma GCC unroll 3                            /* (the conversions side by side in the FPU) */
    for(int k = 0; k < 3; k++) {
#pragma GCC unroll 3
        for(int j = 0; j < 3; j++)
            g->m[3 * k + j] = (int32_t)((basis[k][0] * m[j] + basis[k][1] * m[3 + j] + basis[k][2] * m[6 + j]) * 65536.0f);
        g->t[k] = (int32_t)((basis[k][0] * (o[0] - c[0]) + basis[k][1] * (o[1] - c[1]) + basis[k][2] * (o[2] - c[2])
                             + (k == 2 ? EYE_BACK : 0.0f)) * 256.0f);
    }
    float e[3];
    for(int j = 0; j < 3; j++) e[j] = m[j] * (c[0] - o[0]) + m[3 + j] * (c[1] - o[1]) + m[6 + j] * (c[2] - o[2]);
    face_eye(blk, e[0], e[1], e[2], g);
    g->blk = blk;
    g->mirror = 0;
    g->refl = 0;
}

void track_block_piece(const struct track_cam *cam, const struct track_block *b, struct gu_piece *g)
{
    const float c[3] = { cam->x, cam->y, cam->h };
    float f[3], r[3], u[3];
    cam_frame(cam, f, r, u);
    block_piece(c, f, r, u, b->m, b->o, b->blk, g);
    g->obj = b->file;
    g->phase = b->phase;
}

/* The matrix m and the origin o, in the world, of a block of the piece pc: so is the block's frames or
   position record (<= 0: none), R the world from the piece's frame. Frame fr (>= 0) turns and places the
   block, with fr < 0 only the position record moves it. */
static void block_pose(const uint8_t *obj, int32_t so, int fr, const float R[9], const struct piece *pc,
                       float m[9], float o[3])
{
    float F[9] = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f }, t[3] = { 0.0f, 0.0f, 0.0f };
    if(so > 0 && fr >= 0) {                         /* rows x, up, z: the words down the columns */
        const uint8_t *q = obj + so + 0x1C + 0x30 * fr;
        for(int i = 0; i < 3; i++)
            for(int j = 0; j < 3; j++) F[3 * i + j] = (float)rd16(q + 2 * (3 * j + i)) / 16383.0f;
        for(int i = 0; i < 3; i++) t[i] = (float)rd16(q + 18 + 2 * i);
    } else if(so > 0)
        for(int i = 0; i < 3; i++) t[i] = (float)rd32(obj + so + 0x10 + 4 * i);
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) m[3 * i + j] = R[3 * i] * F[j] + R[3 * i + 1] * F[3 + j] + R[3 * i + 2] * F[6 + j];
        o[i] = (i == 0 ? (float)pc->x : i == 1 ? (float)pc->y : (float)pc->h) +
               R[3 * i] * t[0] + R[3 * i + 1] * t[1] + R[3 * i + 2] * t[2];
    }
}

/* A placement whose mesh has several blocks or moving parts (track 1's football helmet V220:
   its base and the helmet turning on it): block by block, each turned by its frame (0x8ad14:
   a frame's 12 words are the block's whole matrix, the vertex as stored a row multiplied into
   it, 16383 = 1, then its place; frame = frames x clock >> 16, a turn of the clock in 3.33 s;
   the frames turn z up into y up, a mirror) or else moved by its place (the position record),
   the furthest block's key first (0x8ac58). The key is the half of the distance of the block's middle,
   turned by its frame, rounded; a block whose name starts with a capital Z gets 0x2000 more (16384 units
   nearer). That keeps the sails of track 3's windmills (Z1vela) in front of the tower at every turn,
   while the gondolas of track 4's wheel (z1ruot, lower case) pass in front of and behind its frame as
   they go round. Both measured in the original (DOSBox: its table of keys, read while it draws).
   Returns the pieces in gs (MAX_BLOCKS). */
static int multi_pieces(const float c[3], const float f[3], const float r[3], const float u[3],
                        const struct piece *pc, struct gu_piece *gs)
{
    const uint8_t *obj = unr_blob + unr_off[pc->obj];
    int nb = rd32(obj + 0x14), frames = rd32(obj + 0x10), n = 0, ord[MAX_BLOCKS];
    float key[MAX_BLOCKS];
    struct gu_piece tmp[MAX_BLOCKS];
    float ca = sin2048((uint32_t)pc->t + 512), sa = sin2048((uint32_t)pc->t);
    const float R[9] = { ca, 0.0f, -sa, sa, 0.0f, ca, 0.0f, 1.0f, 0.0f };   /* world from the piece's frame */
    int fr = frames > 0 ? (int)(((uint32_t)frames * clock16) >> 16) : -1;
    for(int b = 0; b < nb && b < MAX_BLOCKS; b++) {
        const uint8_t *blk = obj + rd32(obj + 0x18 + 4 * b);
        int32_t so = rd32(blk + 0x0C);
        int moving = so > 0 && fr >= 0;
        float m[9], o[3];                           /* the block's matrix and origin in the world */
        block_pose(obj, so, fr, R, pc, m, o);
        struct gu_piece *g = &tmp[b];
        block_piece(c, f, r, u, m, o, blk, g);
        g->obj = obj;
        g->phase = piece_phase;
        g->mirror = moving;
        float w[3];
        for(int i = 0; i < 3; i++)
            w[i] = o[i] + m[3 * i] * rd16(blk + 4) + m[3 * i + 1] * rd16(blk + 6) + m[3 * i + 2] * rd16(blk + 8) - c[i];
        float dist = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        key[b] = -(float)lrintf(dist * 0.5f) + (blk[0x68] == 'Z' ? 8192.0f : 0.0f);
        int lvl = track_lod_level(dist, 0);       /* level of detail, per block (the middle and the sort key stay level 0's) */
        if(lvl) {
            int32_t off = rd32(obj + 0x18 + 4 * (b + 13 * lvl));
            if(!off) continue;
            block_piece(c, f, r, u, m, o, obj + off, g);
            g->mirror = moving;
        }
        int i = n;
        while(i > 0 && key[ord[i - 1]] > key[b]) { ord[i] = ord[i - 1]; i--; }
        ord[i] = b;
        n++;
    }
    for(int b = 0; b < n; b++) gs[b] = tmp[ord[b]];
    return n;
}

static float refl_rate = 0.008f;

void track_reflection_rate(float k)
{
    refl_rate = k;
}

/* The reflection's offsets of an object (0xc1fc0, before the faces of its first block with
   flag 0x2000): its way since it was last drawn (whole units, as the record keeps its place)
   turned into the view (the original's axes x left, y up, z back), times the rate, added to
   its three accumulators; each sum rounded (to nearest) & 0xff, the first and the third the
   offsets of u and v, as gu_piece.refl's [23:16] and [31:24] */
static uint32_t refl_offsets(const struct track_object *ob, const float f[3], const float r[3], const float u[3])
{
    struct track_refl *s = ob->refl;
    int32_t p[3] = { (int32_t)ob->x, (int32_t)ob->y, (int32_t)ob->h };
    float d[3] = { (float)(p[0] - s->prev[0]), (float)(p[1] - s->prev[1]), (float)(p[2] - s->prev[2]) };
    int32_t n[3];
    if(d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f) {
        const float w[3] = { -(r[0] * d[0] + r[1] * d[1] + r[2] * d[2]), u[0] * d[0] + u[1] * d[1] + u[2] * d[2],
                             -(f[0] * d[0] + f[1] * d[1] + f[2] * d[2]) };
        for(int k = 0; k < 3; k++) {
            float a = w[k] * refl_rate + s->acc[k];
            n[k] = (int32_t)lrintf(a);
            s->acc[k] = a;
            s->prev[k] = p[k];
        }
    } else
        for(int k = 0; k < 3; k++) n[k] = (int32_t)lrintf(s->acc[k]);
    return (uint32_t)(n[0] & 255) << 16 | (uint32_t)(n[2] & 255) << 24;
}

uint32_t track_object_reflection(const struct track_cam *cam, float x, float y, float h, struct track_refl *s)
{
    float f[3], r[3], u[3];
    cam_frame(cam, f, r, u);
    const struct track_object ob = { x, y, h, 0.0f, 0, 0, 0, 0, 0, s, 0 };
    return refl_offsets(&ob, f, r, u);
}

/* The view from the point c along forward f, with right r and up u (unit vectors) */
static void draw(volatile uint8_t *fb, const struct rs_clip *clip, const struct track_cam *cam, const float c[3],
                 const float f[3], const float r[3], const float u[3], struct track_object *objs, int nobj,
                 struct track_stats *st)
{
    uint32_t t_start = now();
    PF_VAR(pf);
    st->pieces = st->faces = st->triangles = st->see_through = st->vertices = st->objects = 0;
    const float cx = c[0], cy = c[1], ch = c[2];
    const float fx = f[0], fy = f[1], fh = f[2], rx = r[0], ry = r[1], rh = r[2];
    const float ux = u[0], uy = u[1], uh = u[2];
    st->cam[0] = (int32_t)cx;
    st->cam[1] = (int32_t)cy;
    st->cam[2] = (int32_t)ch;

    /* the pieces in reach and in view, far to near: vf the depth from the eye */
    const float far_eye = far_z + EYE_BACK;
    const float half_w = (float)(clip->x1 - clip->x0) / 2.0f / FOCAL;
    const int eye = EYE_LINE(clip->y1 - clip->y0);
    const float up = (float)eye / FOCAL;                                       /* above the eye line */
    const float down = (float)(clip->y1 - clip->y0 - eye) / FOCAL;
    int nv = 0;
    /* The cells to look at, in the order of the file (so the pieces come in the order of their numbers): the
       original's walk over the map (STARTL.EXE 0x76d80, docs/screamer2/draw_distance.md) takes only the cells
       that the list of the cell the camera is in names, if that cell has a list (most of the road's cells
       do), else every cell in reach. Without a list the cells whose rows lie in reach are found by their keys
       (a binary search each way), the others by their column; of those a cell is looked at only if it can be
       in reach (the box of its places) and in view (cell_out: its sphere against the planes) */
    const uint16_t *list = 0;
    int ncl = ncells, cc = camera_cell(cx, cy), q0 = 0, c0 = 0, c1 = 255;
    if(cc >= 0 && vcells[cc].nlist) {
        list = vlist + vcells[cc].list;
        ncl = vcells[cc].nlist;
    } else if(cells_keyed) {
        const float reach = __builtin_sqrtf(REACH2);
        int r0 = (((int32_t)(cy - reach) + 0x100000) >> 13) - 1, r1 = (((int32_t)(cy + reach) + 0x100000) >> 13) + 1;
        c0 = (((int32_t)(cx - reach) + 0x100000) >> 13) - 1;
        c1 = (((int32_t)(cx + reach) + 0x100000) >> 13) + 1;
        q0 = r0 > 0 ? first_cell((uint32_t)r0 << 8) : 0;
        ncl = r1 < 255 ? first_cell((uint32_t)(r1 + 1) << 8) : ncells;
    }
    PF_ADD(PF_CULL_SETUP, pf); PF_SET(pf);
    const float side = __builtin_sqrtf(1.0f + half_w * half_w), top = __builtin_sqrtf(1.0f + up * up),
                bottom = __builtin_sqrtf(1.0f + down * down);
    for(int q = q0; q < ncl; q++) {
        const int g = list ? list[q] : q;
        if(!cells[g].count) continue;
        if(!list && cells_keyed) {
            int col = vcells[g].key & 255;
            if(col < c0 || col > c1) continue;
        }
        {                                           /* the whole cell out of reach (in the plane) */
            int wx = cx < cells[g].x0, ex = cx > cells[g].x1, sy = cy < cells[g].y0, ny = cy > cells[g].y1;
            INT_NOW4(wx, ex, sy, ny);
            float gx = wx ? cells[g].x0 - cx : ex ? cx - cells[g].x1 : 0.0f;
            float gy = sy ? cells[g].y0 - cy : ny ? cy - cells[g].y1 : 0.0f;
            if(gx * gx + gy * gy >= REACH2) continue;
        }
        {   /* the whole cell out of view: every placement p of it lies within r0 of the middle m, its reach rad is
               at most rmax, and the placement is out if (n . (p - cam)) > rad (1 + k) for a plane's normal n of
               length s (the tests below, rewritten): so the cell is out if n . (m - cam) > rmax (1 + k) + r0 s
               (and a margin for the rounding). Near: vf + rad < NEAR, far: vf - rad > far_eye (s = 1, k = 0) */
            float dx = 0.5f * (cells[g].x0 + cells[g].x1) - cx, dy = 0.5f * (cells[g].y0 + cells[g].y1) - cy, dh = cells[g].ch - ch;
            float vf = dx * fx + dy * fy + dh * fh + EYE_BACK, vr = dx * rx + dy * ry + dh * rh;
            float vu = dx * ux + dy * uy + dh * uh, r0 = cells[g].r0 + 8.0f, rm = cells[g].rmax;
            int o1 = vf + r0 + rm < NEAR, o2 = vf - r0 - rm > far_eye;
            int o3 = vr - half_w * vf > rm * (1.0f + half_w) + r0 * side, o4 = -vr - half_w * vf > rm * (1.0f + half_w) + r0 * side;
            int o5 = vu - up * vf > rm * (1.0f + up) + r0 * top, o6 = -vu - down * vf > rm * (1.0f + down) + r0 * bottom;
            INT_NOW6(o1, o2, o3, o4, o5, o6);
            if(o1 | o2 | o3 | o4 | o5 | o6) continue;
        }
        for(int i = cells[g].first, end = i + cells[g].count; i < end; i++) {
            const struct cpiece *pc = &cpieces[i];
            int32_t no_mesh;                                /* (its reach -1, as a whole number) */
            memcpy(&no_mesh, &pc->rad, sizeof no_mesh);
            if(no_mesh < 0) continue;
            float rad = pc->rad, dx = pc->x - cx, dy = pc->y - cy, dh = pc->h - ch;
            float d2 = dx * dx + dy * dy + dh * dh;
            float vf = dx * fx + dy * fy + dh * fh + EYE_BACK, vr = dx * rx + dy * ry + dh * rh;
            float vu = dx * ux + dy * uy + dh * uh;
            /* the nearest so far, and out of reach or out of view: all the comparisons asked for at
               once, then one wait for their answers */
            int o0 = d2 >= REACH2, o1 = vf + rad < NEAR, o2 = vf - rad > far_eye;
            int o3 = vr - rad > half_w * (vf + rad), o4 = vr + rad < -half_w * (vf + rad);
            int o5 = vu - rad > up * (vf + rad), o6 = vu + rad < -down * (vf + rad);
            INT_NOW7(o0, o1, o2, o3, o4, o5, o6);
            if(o0 | o1 | o2 | o3 | o4 | o5 | o6) continue;
            vis[nv++] = (depth_key(d2) + cpiece_key[i]) * 65536 + i;
        }
    }
    PF_ADD(PF_CULL_WALK, pf); PF_SET(pf);
    for(int k = 0; k < nobj && k < MAX_OBJS; k++) {
        const struct track_object *ob = &objs[k];
        float dx = ob->x - cx, dy = ob->y - cy, dh = ob->h - ch, rad = ob->radius;
        float vf = dx * fx + dy * fy + dh * fh + EYE_BACK, vr = dx * rx + dy * ry + dh * rh;
        float vu = dx * ux + dy * uy + dh * uh;
        int o1 = vf + rad < NEAR, o2 = vf - rad > far_eye, o3 = vr - rad > half_w * (vf + rad);
        int o4 = vr + rad < -half_w * (vf + rad), o5 = vu - rad > up * (vf + rad), o6 = vu + rad < -down * (vf + rad);
        INT_NOW6(o1, o2, o3, o4, o5, o6);               /* (one wait for the answers, as above) */
        if(o1 | o2 | o3 | o4 | o5 | o6) continue;
        float d2 = dx * dx + dy * dy + dh * dh;
        /* an object (a car) counts 300 nearer (0x7e991), less its mesh's class key (0x7e9a5) */
        vis[nv++] = (depth_key(d2) + 300 + ob->key) * 65536 + npieces + k;
        st->objects++;
    }
    st->nearest = 0xFFFFFFFFu;                  /* (not measured any more: it cost a compare for every placement) */
    /* by key, far first. The list is in the order of i, the keys' low half, and the keys differ:
       sorted by their high half, keeping that order (two passes of 8 bits, radix), it is in the
       order of the whole key, as any sort by it has it */
    PF_ADD(PF_CULL_OBJ, pf); PF_SET(pf);
    static int32_t vis2[MAX_PIECES + MAX_OBJS];
    static int cnt[2][256];                     /* both passes' counts in one go; the sums clear
                                                   them (no memset: picolibc's goes byte by byte) */
    for(int n = 0; n < nv; n++) {
        uint32_t u = (uint32_t)vis[n] ^ 0x80000000u;
        cnt[0][u >> 16 & 255]++;
        cnt[1][u >> 24]++;
    }
    for(int pass = 0; pass < 2; pass++) {
        const int sh = 16 + 8 * pass;
        const int32_t *from = pass ? vis2 : vis;
        int32_t *to = pass ? vis : vis2;
        int at[256];
        for(int d = 0, s = 0; d < 256; d++) {
            at[d] = s;
            s += cnt[pass][d];
            cnt[pass][d] = 0;
        }
        for(int n = 0; n < nv; n++) to[at[((uint32_t)from[n] ^ 0x80000000u) >> sh & 255]++] = from[n];
    }
    PF_ADD(PF_CULL_SORT, pf);
    uint32_t t_pieces = now();
    st->cyc_cull = t_pieces - t_start;
#ifdef TRACK_DUMP                               /* the PC renderer (tools/render.sh): the list drawn */
    for(int k = 0; k < nv; k++)
        if((vis[k] & 0xFFFF) < npieces)
            printf("drawn piece obj %d at %d %d %d\n", (int)pieces[vis[k] & 0xFFFF].obj, (int)pieces[vis[k] & 0xFFFF].x,
                   (int)pieces[vis[k] & 0xFFFF].h, (int)pieces[vis[k] & 0xFFFF].y);
        else
            printf("drawn object at %.0f %.0f %.0f\n", objs[(vis[k] & 0xFFFF) - npieces].x,
                   objs[(vis[k] & 0xFFFF) - npieces].h, objs[(vis[k] & 0xFFFF) - npieces].y);
#endif

    struct su_view view;
    struct gu_stats gst = { 0, 0, 0, 0 };
    if(mirror_pass) track_view_mirror(clip, &view);
    else track_view(clip, &view);
    uint32_t t_out = 0;
    for(int n = 0; n < nv; n++) {
        const int vi = vis[n] & 0xFFFF;
        PF_VAR(pb);
        if(vi >= npieces) {                         /* an object: its blocks as they come */
            struct track_object *ob = &objs[vi - npieces];
            if(ob->make) ob->make(ob->id, cam, ob);
            uint32_t refl = ob->refl_phase & 0xFFFF;
            for(int b = 0; b < ob->n && ob->refl; b++)
                if(rd32(ob->b[b].blk + 0x18) & 0x2000) {
                    refl |= refl_offsets(ob, f, r, u);
                    break;
                }
            for(int b = 0; b < ob->n; b++) {
                struct gu_piece g;
                block_piece(c, f, r, u, ob->b[b].m, ob->b[b].o, ob->b[b].blk, &g);
                g.obj = ob->b[b].file;
                g.phase = ob->b[b].phase;
                g.refl = refl;
                st->pieces++;
                uint32_t t0 = now();
                if(track_mode == TRACK_HW) track_hw_piece(&g);
                else gu_draw(fb, clip, &view, mlb, &g, &gst);
                t_out += now() - t0;
                PF_PIECE(1);
            }
            PF_ADD(PF_PC_OBJ, pb);
            continue;
        }
        const struct piece *pc = &pieces[vi];
        if(multi[pc->obj]) {
            struct gu_piece gs[MAX_BLOCKS];
            int nb = multi_pieces(c, f, r, u, pc, gs);
            for(int b = 0; b < nb; b++) {
                st->pieces++;
                uint32_t t0 = now();
                if(track_mode == TRACK_HW) track_hw_piece(&gs[b]);
                else gu_draw(fb, clip, &view, mlb, &gs[b], &gst);
                t_out += now() - t0;
                PF_PIECE(2);
            }
            PF_ADD(PF_PC_MULTI, pb);
            continue;
        }
        struct gu_piece g;
        g.phase = piece_phase;
        g.mirror = 0;
        g.refl = 0;
        g.blk = block_of(pc->obj, &g.obj);
        float ca = sin2048((uint32_t)pc->t + 512), sa = sin2048((uint32_t)pc->t);
        float dx = pc->x - cx, dy = pc->y - cy, dh = pc->h - ch;

        /* level of detail: the block's table entry 13 L on (level 0's middle gives the distance;
           0 = no block at that level = nothing drawn) */
        {
            float lx = rd16(g.blk + 4), ly = rd16(g.blk + 6), lz = rd16(g.blk + 8);
            float wx = dx + lx * ca - lz * sa, wy = dy + lx * sa + lz * ca, wh = dh + ly;
            int lvl = track_lod_level(sqrtf(wx * wx + wy * wy + wh * wh), 0);
            if(lvl) {
                int32_t off = rd32(g.obj + 0x18 + 4 * 13 * lvl);
                if(!off) { PF_ADD(PF_PC_STATIC, pb); continue; }
                g.blk = g.obj + off;
            }
        }

        /* local to view: per axis A, A . D + x (Ax cos + Ay sin) + y Ah + z (Ay cos - Ax sin) */
        const float m[9] = { rx * ca + ry * sa, rh, ry * ca - rx * sa,
                             ux * ca + uy * sa, uh, uy * ca - ux * sa,
                             fx * ca + fy * sa, fh, fy * ca - fx * sa };
        const float o[3] = { dx * rx + dy * ry + dh * rh, dx * ux + dy * uy + dh * uh,
                             dx * fx + dy * fy + dh * fh + EYE_BACK };
#pragma GCC unroll 9                            /* (the conversions side by side in the FPU) */
        for(int k = 0; k < 9; k++) g.m[k] = (int32_t)(m[k] * 65536.0f);
        for(int k = 0; k < 3; k++) g.t[k] = (int32_t)(o[k] * 256.0f);

        /* the camera in the piece's frame picks the row, the eye tells the faces turned away */
        face_eye(g.blk, -dx * ca - dy * sa, -dh, dx * sa - dy * ca, &g);
        st->pieces++;
        uint32_t t0 = now();
        if(track_mode == TRACK_HW) track_hw_piece(&g);
        else gu_draw(fb, clip, &view, mlb, &g, &gst);
        t_out += now() - t0;
        PF_PIECE(0);
        PF_ADD(PF_PC_STATIC, pb);
    }
    st->faces = gst.faces;
    st->triangles = gst.triangles;
    st->see_through = gst.see_through;
    st->vertices = gst.vertices;
    st->cyc_out = t_out;
#ifdef S2_PROF
    prof_t[PF_PUSH] += t_out;
    prof_n[PF_PUSH] += st->pieces;
#endif
    st->cyc_piece = now() - t_pieces - t_out;
}

void track_draw_cam(volatile uint8_t *fb, const struct rs_clip *clip, const struct track_cam *cam,
                    struct track_object *objs, int nobj, struct track_stats *st)
{
    const float c[3] = { cam->x, cam->y, cam->h };
    float f[3], r[3], u[3];
    cam_frame(cam, f, r, u);
    draw(fb, clip, cam, c, f, r, u, objs, nobj, st);
}

void track_draw_mirror(volatile uint8_t *fb, const struct rs_clip *clip, const struct track_cam *cam,
                       struct track_object *objs, int nobj, struct track_stats *st)
{
    const float c[3] = { cam->x, cam->y, cam->h };
    float f[3], r[3], u[3];
    cam_frame(cam, f, r, u);
    for(int k = 0; k < 3; k++) r[k] = -r[k];
    mirror_pass = 1;
    const float far_keep = far_z, reach_keep = reach2;
    far_z = far_keep * ((float)MIRROR_FAR_PCT / 100.0f);
    reach2 = (far_z + REACH_PLUS) * (far_z + REACH_PLUS);
    draw(fb, clip, cam, c, f, r, u, objs, nobj, st);
    far_z = far_keep;
    reach2 = reach_keep;
    mirror_pass = 0;
}

void track_draw(volatile uint8_t *fb, const struct rs_clip *clip, uint32_t pos, struct track_stats *st)
{
    /* the camera on the player's line, looking LOOK points ahead and a little down */
    if(npath < LOOK + 2) return;                /* (Screamer 2's images have no path) */
    uint32_t i0 = (pos >> 8) % (uint32_t)npath, i1 = (i0 + 1) % (uint32_t)npath;
    uint32_t il = (i0 + LOOK) % (uint32_t)npath;
    float fr = (float)(pos & 255) / 256.0f;
    float cx = path[i0].x + (path[i1].x - path[i0].x) * fr;
    float cy = path[i0].y + (path[i1].y - path[i0].y) * fr;
    float ch = path[i0].h + (path[i1].h - path[i0].h) * fr + CAM_UP;
    float fx = path[il].x - cx, fy = path[il].y - cy, fh = path[il].h + CAM_UP - 250.0f - ch;
    float l = __builtin_sqrtf(fx * fx + fy * fy + fh * fh);
    fx /= l; fy /= l; fh /= l;
    float rx = fy, ry = -fx;                    /* right = forward x up, level */
    l = __builtin_sqrtf(rx * rx + ry * ry);
    rx /= l; ry /= l;
    const float c[3] = { cx, cy, ch }, f[3] = { fx, fy, fh }, r[3] = { rx, ry, 0.0f };
    const float u[3] = { ry * fh, -rx * fh, rx * fy - ry * fx };     /* up = right x forward */
    draw(fb, clip, 0, c, f, r, u, 0, 0, st);
}

/* the animation clock set to a (0 .. 1 of its turn) by something that runs it at its own pace: the options room's textures play at 0.2 a turn a second (STARTL.EXE 0x39000), the
   race's at 0.3. */
void track_clock_set(float a)
{
    anim = a;
    clock16 = (uint32_t)(anim * 65535.0f);
    piece_phase = clock16 & 0x3FFF;
}
