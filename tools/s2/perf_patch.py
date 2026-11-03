"""Counters into COPIES of the firmware's track.c, geom.c, setup.c and raster.c (tools/s2/perf.h).

Usage: python -I tools/s2/perf_patch.py <dir with the copies> [s2]
  s2: also the three constants Screamer 2's tracks need (docs/screamer2/comparison.md, E3):
      MAX_PIECES 4096, MAX_OBJ 1024, MAX_BLOCKS 16 (a car as a track piece has twelve blocks),
      and the mesh class word 0 (Screamer 2's object header has none).
Every edit asserts that the text it replaces is there once, so a changed firmware fails loudly
instead of silently counting something else. The firmware files themselves are never touched.
"""
import sys


def edit(path, pairs):
    t = open(path, newline="").read()
    for old, new in pairs:
        assert t.count(old) == 1, f"{path}: {old!r} found {t.count(old)} times"
        t = t.replace(old, new)
    open(path, "w", newline="").write(t)


def main():
    d = sys.argv[1]
    s2 = len(sys.argv) > 2 and sys.argv[2] == "s2"
    track = [
        ("            int g = i / GROUP;\n", "            int g = i / GROUP;\n            perf_c.groups++;\n"),
        ("                i += GROUP - 1;\n                continue;\n",
         "                i += GROUP - 1;\n                perf_c.grp_skipped++;\n                continue;\n"),
        ("        const struct cpiece *pc = &cpieces[i];\n        int32_t no_mesh;",
         "        const struct cpiece *pc = &cpieces[i];\n        perf_c.tested++;\n        int32_t no_mesh;"),
        ("        if(no_mesh < 0) continue;\n", "        if(no_mesh < 0) continue;\n        perf_c.tested_mesh++;\n"),
        ("        vis[nv++] = (KEY(d2) + cpiece_key[i]) * 65536 + i;\n",
         "        vis[nv++] = (KEY(d2) + cpiece_key[i]) * 65536 + i;\n        perf_c.visible++;\n"),
        ("        st->objects++;\n", "        st->objects++;\n        perf_c.obj_visible++;\n"),
        ("            if(ob->make) ob->make(ob->id, cam, ob);\n",
         "            if(ob->make) ob->make(ob->id, cam, ob);\n            perf_c.obj_blocks += (uint32_t)ob->n;\n"),
        ("            int nb = multi_pieces(c, f, r, u, pc, gs);\n",
         "            int nb = multi_pieces(c, f, r, u, pc, gs);\n            perf_c.multi_blocks += (uint32_t)nb;\n"),
    ]
    track += [("#define REACH2      2704000000.0f\n", "static float reach2_v = 2704000000.0f;\n#define REACH2      reach2_v\n")]
    if s2:
        track += [
            ("#define MAX_PIECES  2560 ", "#define MAX_PIECES  4096 "),
            ("#define MAX_OBJ     512 ", "#define MAX_OBJ     1024"),
            ("#define MAX_BLOCKS  8 ", "#define MAX_BLOCKS  16"),
            ("int32_t cl = rd32(obj + 0x32C); ", "int32_t cl = 0; (void)obj; "),
        ]
    edit(f"{d}/track.c", track)
    with open(f"{d}/track.c", "a", newline="") as fh:       # the view distances, set by framestats (FAR, REACH)
        fh.write("\nvoid perf_set_far(float far, float reach)\n{\n    far_z = far;\n    reach2_v = reach * reach;\n}\n")
    edit(f"{d}/geom.c", [
        ("    st->vertices += (uint32_t)nvert;\n",
         "    st->vertices += (uint32_t)nvert;\n    perf_c.gu_pieces++;\n    perf_c.verts += (uint32_t)nvert;\n"
         "    perf_gu_rd(p->blk, 0x30);\n    perf_gu_rd(verts, (uint32_t)nvert * 6);\n"),
        ("        for(int i = 0; i < nvert && i < cnt; i++) reflection(p, vt, nrm + 4, i);\n",
         "        perf_c.refl_verts += (uint32_t)(nvert < cnt ? nvert : cnt);\n        perf_gu_rd(nrm, 4 + 12 * (uint32_t)cnt);\n"
         "        for(int i = 0; i < nvert && i < cnt; i++) reflection(p, vt, nrm + 4, i);\n"),
        ("        const uint8_t *fc = p->blk + fo;\n",
         "        const uint8_t *fc = p->blk + fo;\n        perf_c.faces_row++;\n        perf_gu_rd(e, 2);\n        perf_gu_rd(fc, 0x20);\n"),
        ("        const uint8_t *c0 = verts + ru16(fc + 8);\n", "        const uint8_t *c0 = verts + ru16(fc + 8);\n        perf_gu_rd(c0, 6);\n"),
        ("+ bz * rd16(fc + 0x1C) < 0) != (p->mirror != 0)) continue;",
         "+ bz * rd16(fc + 0x1C) < 0) != (p->mirror != 0)) { perf_c.back++; continue; }"),
        ("        const uint8_t *mat = mlb + rd16(fc + 4);\n", "        const uint8_t *mat = mlb + rd16(fc + 4);\n        perf_gu_rd(mat, 16);\n"),
        ("            const uint8_t *trec = mlb + rd32(mat + 4);\n", "            const uint8_t *trec = mlb + rd32(mat + 4);\n            perf_gu_rd(trec, 12);\n"),
        ("            c[j].f = vt[vi % GU_MAX_VERT].f;\n", "            c[j].f = vt[vi % GU_MAX_VERT].f;\n            perf_gu_rd(uvs + vo, 4);\n"),
        ("        st->faces++;\n", "        st->faces++;\n        perf_fade(c[0].f, c[1].f, c[2].f);\n"),
    ])
    edit(f"{d}/setup.c", [
        ("    if(all) return 0;                       /* wholly outside one plane */\n",
         "    perf_c.polys++;\n    if(all) { perf_c.clip_out++; return 0; }\n    if(any) perf_c.clip_any++;\n"
         "    if(any & 1) perf_c.clip_near++;\n    if(any & 32) perf_c.clip_far++;\n"),
    ])
    edit(f"{d}/raster.c", [
        ("    if(r0 >= r1) return 0;\n", "    if(r0 >= r1) return 0;\n    perf_c.walk_tris++;\n    perf_c.rows += (uint32_t)(r1 - r0);\n"),
        ("        if(x0 < x1) n += span(fb, r, x0, x1, ctx);\n",
         "        if(x0 < x1) { perf_c.spans++; perf_c.px_span += (uint32_t)(x1 - x0); n += span(fb, r, x0, x1, ctx); }\n"),
        ("        s.x += s.slope;\n    }\n    return n;\n}\n", "        s.x += s.slope;\n    }\n    perf_c.px_drawn += n;\n    return n;\n}\n"),
        ("    return walk(fb, clip, v, span_flat, &colour, 0);\n", "    perf_c.tris_flat++;\n    return walk(fb, clip, v, span_flat, &colour, 0);\n"),
        ("    struct texfx_ctx c = { s, tex };\n",
         "    struct texfx_ctx c = { s, tex };\n    perf_c.tris_tex++;\n    perf_c.tris_blend += tex->routine == 224;\n"),
    ])


if __name__ == "__main__":
    main()
