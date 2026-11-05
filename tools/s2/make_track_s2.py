"""The reference for the Screamer 2 track image (firmware/track_build.c, track_build_s2()): a track
image (firmware/track.h, "SCRK" version 7) from a Screamer 2 track, built with the helpers of
tools/make_track.py UNCHANGED (placements, ground index, road points, walls, PCX, the car models'
wheel places). tools/s2/check_track_build_s2.sh compares the firmware's own build with this one, byte for
byte. The image stays under build/ (game data, never committed).

What Screamer 2's files need beyond the names (each marked ADAPTER below):
  - file names: GFX/MAPn{L,H}{0,1}1.DAT (L/H = detail set, 0/1 = day/night look, 1 = 256 colours),
    DATA/MAPn.DAT (S1: GFX/MAPnH.DAT)
  - no camera path, no markers, no opponents, no race screen, no start grid: the viewer needs none of
    them (empty sections; the player starts at road point 0, MMP array 2)
  - stand-in rows for CARS_DATA (the cars' physics is new in Screamer 2), one per car model, naming the car
  - TRACK_DATA of MAPnDAT.DTR: the far plane (header 0x70) and the background palette entry (0x7C)
  - cars: the four cars of the grid, each its team's: AUTO<car>.3DX and .MLB of GFX/CARS<team>{look}1.DAT, in
    grid order (the player's first; S2CARS below, default team 1 car 1, team 2 car 2, team 3 car 3, team 4
    car 4). Their material libraries are not in the track's: each car's .MLB is appended after the track's,
    its material records (16 bytes) go into a section in front of the track's MLB (the geometry unit reads a
    face's material at a signed 16-bit offset from the MLB's start, so they have to lie within 32 KB of it,
    below), and the cars' faces point there (docs/screamer2/comparison.md, "Cars' libraries"). A race draws
    nothing but these four, so the other cars of the teams are not in the image
  - the cars' windows (routine 160): the game draws them with the first material of GFX/ENVP{n}{look}1.DAT's
    ENV.MLB (a 128 x 128 picture, 1 to 3 frames; its texel address is masked with 0x7f7f), not with the
    car's own placeholder. The hardware wraps routine 160's u, v at 256, so each frame is stored 2 x 2
    tiled as 256 x 256 (env_picture() below) after the cars' libraries, inside the MLB section, with a texture
    record of its own, and the records of the cars' routine-160 materials point to that record. The image
    stays version 7: no new section, the MLB section only grows by 64 KB a frame

Image layout, version 7 (the version 6 of tools/make_track.py with the car materials in front):
  0x100  car materials (the records of the four cars' .MLB, texture records' offsets moved to the appended
         libraries; the routine-160 records' texture record is the reflection's, below)
         then the track's MAPn.MLB, padded to 256 bytes, then AUTO1.MLB .. AUTO5.MLB each padded to 256,
         then the reflection picture: a texture record (frames, the ENV record's word +4, the frames' offsets)
         padded to 256 bytes and the frames, 64 KB each (256 x 256, rows of 256); the header's MLB offset
         (0x08) is the MLB, its size (0x0C) all of it
         The MLB section also holds MISC.MLB (a race's one more library after the cars': its 11 to 48 material records go into the section in front, like a car's),
         and after the reflection picture the see-through copies and the effects block (tools/s2/smg_ref.py, firmware/smg.c): every material of routine 208 or 224
         turned into routine 224 on a copy of its texture whose texels are the hardware's rows, then the meshes of the shadow and the two smokes (faces onto MISC's
         records), the table of 64 x 256 and a directory of 256 bytes ('S2FX', 1, the table's offset, the meshes', the rows used, the texels found), the section's last
  then   UNR, palette, placements (and after them the cells' visibility table, visibility() below), camera path
         (empty), ground index, markers (empty), cars (one stand-in row per car model, its livery word the car:
         team * 256 + car),
         car models (4 .3DX, one per grid slot, faces moved onto the car materials), road points, walls, race screen
         (empty), backdrop, opponents (empty), road kinds; header 0x5C the start (road point 0), 0x70 the far plane
         (positive units), 0x74 the offset of the road kinds (one byte a placement, the last section), 0x7C the background colour, as tools/make_track.py otherwise.

Usage: python tools/s2/make_track_s2.py <Screamer 2 folder> <track 1..7> <out.bin> [H|L] [look 0|1]
  S2START_ROAD=k in the environment: the player's start place is road point k (MMP array 2) instead of
  point 0 (a chosen frame for the simulation)
  S2CARS=team:car,... in the environment: the cars of the grid, 1 to 5 of them (a race has four), team 1..4 and
  car 1..5 as the files number them (CARS<team>, AUTO<car>), the player's first; default 1:1,2:2,3:3,4:4
  S2ROOM=1 in the environment: a menu's room instead of a track (the options room, docs/screamer2/menus.md row 8): the "track" number is the team,
  the entries MAP<team>.* are those of GFX/MENU<team>1.DAT, and the image is the scenery only: no cars (so no car materials in front of the MLB, no
  models, no reflection picture), no road points or walls (the map has none), no TRACK_DATA (the menus' far plane 43,808, colour 0)
"""
import os
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
tools = os.path.join(here, "..")
sys.path.insert(0, tools)
import datfile
sys.path.insert(0, here)
import smg_ref

# tools/make_track.py runs main() when imported; take everything above that call
src = open(os.path.join(tools, "make_track.py")).read()
ns = {"__file__": os.path.join(tools, "make_track.py"), "__name__": "make_track"}
exec(compile(src.rsplit("\nmain()", 1)[0], "make_track.py", "exec"), ns)
placements, ground_index, cell_arrays, car_model, pcx, cells = (
    ns[k] for k in ("placements", "ground_index", "cell_arrays", "car_model", "pcx", "cells"))
BLOCK = ns["BLOCK"]
FIRST = 0x100
GRID = "1:1,2:2,3:3,4:4"                    # the cars of the grid, team:car, the player's first
i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
i16 = lambda b, o: struct.unpack_from("<h", b, o)[0]
pad256 = lambda n: (n + 255) & ~255


VIS_MAGIC = 0x31534956                      # "VIS1"


def visibility(mmp):
    """ADAPTER: Screamer 2's cells carry a fifth array, run 4: the list of the cells that may be seen from
    the cell (entries of 4 bytes, row << 16 | column, count at +0x24, offset at +0x10), sorted by row and
    column. STARTL.EXE's walk over the map (0x76d80) takes the placements of those cells only (and then the
    window and the distance), as long as the camera's cell has such a list (docs/screamer2/draw_distance.md).
    The table that goes after the placements in their section (firmware/tb_mmp.h, tb_mmp_vis()):
      u32 "VIS1", u32 cells, u32 list entries
      cells x { u16 key (row << 8 | column), first (the cell's first placement, as the placements are
                numbered), count, list (its first entry), entries }
      entries x u16: cell numbers (as the cells are listed), the cells the list names that exist"""
    i32 = lambda o: struct.unpack_from("<i", mmp, o)[0]
    cs, first = [], 0
    for gy in range(256):
        row = i32(4 * gy)
        if row == -1:
            continue
        for gx in range(256):
            c = i32(row + 4 * gx)
            if c != -1:
                n = i32(c + 0x14) + i32(c + 0x18)
                cs.append((gy << 8 | gx, c, first, n))
                first += n
    index = {key: k for k, (key, _, _, _) in enumerate(cs)}
    cell_rec, entries = b"", []
    for key, c, first, n in cs:
        ids = []
        for k in range(max(i32(c + 0x24), 0)):
            e = struct.unpack_from("<I", mmp, i32(c + 0x10) + 4 * k)[0]
            if (e >> 16) < 256 and (e & 0xFFFF) < 256 and ((e >> 16) << 8 | (e & 0xFFFF)) in index:
                ids.append(index[(e >> 16) << 8 | (e & 0xFFFF)])
        assert first + n < 65536 and len(entries) + len(ids) < 65536
        cell_rec += struct.pack("<5H", key, first, n, len(entries), len(ids))
        entries += ids
    return (struct.pack("<3I", VIS_MAGIC, len(cs), len(entries)) + cell_rec +
            struct.pack(f"<{len(entries)}H", *entries))


def s2_track_data(dtr):
    """ADAPTER: Screamer 2's TRACK_DATA is a list of 'd' numbers with comments: far plane (negative),
    background rgb, background 256 (palette entry), asmoke rgb, a2smoke rgb, tyre pressure (the first eleven are read)"""
    s = dtr.decode("latin-1")
    body = s[s.index("TRACK_DATA"):]
    nums = []
    for line in body.splitlines()[1:]:
        t = line.strip()
        if t.startswith("*") or not t:
            continue
        if t.startswith("END_LIST") or t.startswith("*---"):
            break
        nums += [int(v[1:]) for v in t.split() if v[0] == "d"]
        if len(nums) >= 11:
            break
    return nums[:11]


def mlb_records(mlb):
    """Material records of an .MLB: the table of 16-byte records runs up to the first texture record
    (the loop of the PC port's mlb_read)"""
    end, k = len(mlb), 0
    while (k + 1) * 16 <= end:
        ro = i32(mlb, 16 * k + 4)
        if ro > 0 and ro + 12 <= len(mlb) and ro < end:
            end = ro
        k += 1
    return k


def car_library(mlb, at, env_at):
    """A car's .MLB placed at offset `at` from the merged MLB's start: (number of material records, the
    records with their texture records' offsets moved, the file with the texture records' pixel offsets
    moved). A texture record is n, one word, then n offsets (one if n < 1); two materials may share one.
    A routine-160 material (a window) gets the reflection's texture record, at env_at"""
    nrec = mlb_records(mlb)
    out, recs, seen = bytearray(mlb), bytearray(), set()
    for k in range(nrec):
        rec = bytearray(mlb[16 * k:16 * k + 16])
        ro = i32(mlb, 16 * k + 4)
        if ro > 0 and ro + 12 <= len(mlb):
            struct.pack_into("<I", rec, 4, (env_at if i32(mlb, 16 * k) == 160 else ro + at) & 0xFFFFFFFF)
            if ro not in seen:
                seen.add(ro)
                n = i32(mlb, ro)
                for fr in range(n & 0xFFFF if n > 0 else 1):
                    p = ro + 8 + 4 * fr
                    if p + 4 > len(mlb):
                        break
                    struct.pack_into("<I", out, p, (i32(mlb, p) + at) & 0xFFFFFFFF)
        recs += rec
    return nrec, bytes(recs), bytes(out)


ENV_HEAD = 256                              # the reflection's texture record, padded to this
ENV_FRAME = 256 * 256


def env_picture(mlb, at):
    """ADAPTER: the reflection's block from ENV.MLB of GFX/ENVP{n}{look}1.DAT, to lie at offset `at` of the
    merged MLB. The first material is routine 160, 128 x 128; its texture record is n (1 to 3 frames), a word
    (kept), the frames' offsets; a frame is 128 rows of 256 bytes, the picture in the first 128 columns. The
    game masks a texel's address with 0x7f7f (STARTL.EXE 0xcd771, `and ebx,0x7f7f`) where the hardware masks
    u and v with 0xff, and a window's u, v are whole texels (0xcd118: the reflection's value << 3, one texel
    each, as in firmware/geom.c): a frame tiled 2 x 2 into 256 x 256 reads alike. The block is the texture
    record (n, the word, the frames' offsets) padded to ENV_HEAD, then the tiled frames"""
    assert i32(mlb, 0) == 160 and (i32(mlb, 8), i32(mlb, 12)) == (128, 128), "ENV.MLB: first material"
    ro = i32(mlb, 4)
    n = i32(mlb, ro)
    n = n & 0xFFFF if n > 0 else 1
    assert 1 <= n <= 8, f"ENV.MLB: {n} frames"
    head, body = struct.pack("<2i", n, i32(mlb, ro + 4)), b""
    for fr in range(n):
        o = i32(mlb, ro + 8 + 4 * fr)
        assert 0 <= o and o + 127 * 256 + 128 <= len(mlb), "ENV.MLB: frame"
        rows = b"".join(mlb[o + 256 * r:o + 256 * r + 128] * 2 for r in range(128))
        head += struct.pack("<I", at + ENV_HEAD + ENV_FRAME * fr)
        body += rows * 2
    return head + bytes(ENV_HEAD - len(head)) + body


def car_faces_onto(x3d, delta):
    """A .3DX car with every face's material offset (face +4, from the car's own table) moved by delta
    (negative: the car's records lie below the merged MLB's start). Every block of the header's table at the
    three levels of detail (entry b + 13 L), every face of its 16 rows; moved only while it is still >= 0, so a
    face in several rows is moved once"""
    x = bytearray(x3d)
    nb = i32(x, 0x14)
    assert 0 <= nb <= 44, f"{nb} blocks"
    for lvl in range(3):
        for b in range(nb):
            at = 0x18 + 4 * (b + 13 * lvl)
            if at + 4 > len(x):
                continue
            blk = i32(x, at)
            if blk <= 0 or blk + 0x68 > len(x):
                continue
            for r in range(16):
                ro = i32(x, blk + 0x28 + 4 * r)
                if ro <= 0:
                    continue
                for j in range(1024):
                    p = blk + ro + 2 * j
                    if p + 2 > len(x) or u16(x, p) == 0xFFFF:
                        break
                    f = blk + u16(x, p) + 4
                    if f + 2 <= len(x) and i16(x, f) >= 0:
                        m = i16(x, f) + delta
                        assert -32768 <= m < 0, f"material offset {m}"
                        struct.pack_into("<h", x, f, m)
    return bytes(x)


def road_kinds(mmp):
    """One byte per placement, in the order of placements(): the low byte of the record's word +0x3C (ROAD KIND id)"""
    out = bytearray()
    for c in cells(mmp):
        n0, n1 = i32(mmp, c + 0x14), i32(mmp, c + 0x18)
        for k in list(range(n0, n0 + n1)) + list(range(n0)):
            v = i32(mmp, c + 0x28 + 64 * k + 0x3C)
            assert 0 <= v < 256, f"road kind {v}"
            out.append(v)
    return bytes(out)


def main():
    root, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    detail = sys.argv[4] if len(sys.argv) > 4 else "H"
    look = sys.argv[5] if len(sys.argv) > 5 else "0"
    room = bool(os.environ.get("S2ROOM"))                               # ADAPTER: a menu's room (see the header)
    gfx = os.path.join(root, "GFX", f"MENU{n}1.DAT" if room else f"MAP{n}{detail}{look}1.DAT")      # ADAPTER: the name
    g = datfile.read(gfx)
    track = f"MAP{n}"
    mlb = datfile.extract(g, track + ".MLB")
    unr = datfile.extract(g, track + ".UNR")
    col = datfile.extract(g, track + ".COL")
    mmp = datfile.extract(g, track + ".MMP")
    if room:
        tdata = [-43808, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1]            # (no asmoke and a2smoke: only the shadow's rows)
    else:
        d = datfile.read(os.path.join(root, "DATA", f"MAP{n}.DAT"))
        tdata = s2_track_data(datfile.extract(d, track + "DAT.DTR"))      # ADAPTER: MAPnDAT.DTR
    far, colour = -tdata[0], tdata[4]
    pal = b"".join(struct.pack("<I", col[3 * i] * 4 << 16 | col[3 * i + 1] * 4 << 8 | col[3 * i + 2] * 4) for i in range(256))
    pl, run0 = placements(mmp)
    ground, nground = ground_index(pl, run0)
    dirs, walls = cell_arrays(mmp)

    # ADAPTER: the grid's cars, each its team's, their libraries merged (see the header)
    pick = [] if room else [tuple(int(v) for v in c.split(":")) for c in os.environ.get("S2CARS", GRID).split(",")]
    assert (room or 1 <= len(pick) <= 5) and all(1 <= t <= 4 and 1 <= c <= 5 for t, c in pick), f"S2CARS {pick}"
    CARS = len(pick)
    # one stand-in row per car model, its livery word the car: team * 256 + car (s2car.c finds the wheels by it)
    cars = b"".join(struct.pack("<12si4f2i7f", b"S2 STANDIN", t << 8 | c, 150.0, 9000.0, 1.0, 1.0, 1, 4, 0, 3.0, 2.0, 1.5, 1.2, 1.0, 0.8)
                    for t, c in pick)
    teams = {t: datfile.read(os.path.join(root, "GFX", f"CARS{t}{look}1.DAT")) for t, _ in pick}
    car_mlb = [datfile.extract(teams[t], f"AUTO{c}.MLB") for t, c in pick]
    x3d = [datfile.extract(teams[t], f"AUTO{c}.3DX") for t, c in pick]
    # the track's MISC.MLB (the materials of its small objects: the scenery's moving things, docs/development.md "Animated scenery") is one more library after the cars'
    libs = list(car_mlb)
    if not room:                                                        # ADAPTER: MISC.MLB is one more library (the shadow's and the smokes' materials, the rotor's ...)
        libs.append(datfile.extract(g, "MISC.MLB"))
    nrec = [mlb_records(m) for m in libs]
    cmat_size = 16 * sum(nrec)
    mlb_off = FIRST + pad256(cmat_size)
    assert mlb_off - FIRST <= 32768, "car materials too far below the MLB"
    lib_at, at = [], pad256(len(mlb))
    for m in libs:
        lib_at.append(at)
        at += pad256(len(m))
    env_at = at                                                         # the windows' picture, after the libraries
    cmat, mlbsec = b"", mlb + bytes(pad256(len(mlb)) - len(mlb))
    for k in range(len(libs)):
        _, recs, lib = car_library(libs[k], lib_at[k], env_at)
        cmat += recs
        mlbsec += lib + bytes(pad256(len(lib)) - len(lib))
    if not room:
        env = datfile.extract(datfile.read(os.path.join(root, "GFX", f"ENVP{n}{look}1.DAT")), "ENV.MLB")   # ADAPTER
        mlbsec += env_picture(env, env_at)
    # ADAPTER: the see-through step (tools/s2/smg_ref.py): the textures of routine 208 / 224 copied with their texels as rows of the hardware's 64-row table, the effects'
    # block at the end of the MLB section (the meshes of the shadow and the two smokes, faces onto MISC's records, the table, the directory)
    meshes = [] if room else [car_faces_onto(datfile.extract(g, nm), FIRST + 16 * sum(nrec[:CARS]) - mlb_off) for nm in ("ASHADOW.3DX", "ASMOKE.3DX", "A2SMOKE.3DX")]
    pal6 = [tuple(col[3 * i:3 * i + 3]) for i in range(256)]
    cmat, mlbsec, rowmap, alive = smg_ref.process(cmat, sum(nrec), mlbsec, mlb_records(mlb), pal6, tdata[5:8], tdata[8:11], not room, meshes)
    models, files, at = b"", b"", 24 * CARS
    for k in range(CARS):
        delta = FIRST + 16 * sum(nrec[:k]) - mlb_off
        f = car_faces_onto(x3d[k], delta)
        models += car_model(f) + struct.pack("<2I", at + len(files), len(f))
        files += f + b"\0" * (-len(f) % 256)
    models += files

    sr = os.environ.get("S2START_ROAD")                                 # a chosen start for the simulation
    sa, sx_, sy_ = (0.0, 0, 0) if not dirs else struct.unpack("<f2i", dirs[int(sr) if sr else 0])
    names = {e.name.upper() for e in datfile.entries(g)}
    if track + ".BCK" in names:
        bw, bh, back = pcx(datfile.extract(g, track + ".BCK"))
        assert (bw, bh) == (512, 256), f"backdrop {bw} x {bh}"
    else:                                                               # track 7 is indoors: no backdrop
        back = bytes([colour]) * (512 * 256)
    back += back[-512:]
    hud = bytes(8 + 16 + 26 * 32)                                       # ADAPTER: no race screen (0 images, 0 fonts)
    opp = bytes(8)                                                      # ADAPTER: no opponents

    sections = [cmat, mlbsec, unr, pal, b"".join(struct.pack("<5i", *p) for p in pl) + visibility(mmp), b"", ground, b"",
                cars, models, b"".join(dirs), b"".join(walls), hud, back, opp, road_kinds(mmp)]
    counts = [0, len(mlbsec), len(unr), 0, len(pl), 0, nground, 0, CARS, CARS, len(dirs), len(walls), len(hud), 0, 0, len(pl)]
    offs, pos = [], FIRST
    for s in sections:
        offs.append(pos)
        pos = pad256(pos + len(s))
    blob = bytearray(pos)
    for o, s in zip(offs, sections):
        blob[o:o + len(s)] = s
    assert offs[1] == mlb_off
    crcs = [zlib.crc32(blob[o:o + BLOCK]) for o in range(FIRST, pos, BLOCK)]
    o, c = offs[1:], counts[1:]          # the header's sections: from the MLB on
    head = b"SCRK" + struct.pack("<I", 7) + struct.pack("<2I", o[0], c[0]) + \
        struct.pack("<2I", o[1], c[1]) + struct.pack("<I", o[2]) + \
        b"".join(struct.pack("<2I", o[k], c[k]) for k in range(3, 11)) + \
        struct.pack("<2if", sx_, sy_, sa) + struct.pack("<2I", o[11], c[11]) + \
        struct.pack("<2i", far, o[14]) + struct.pack("<2I", o[12], colour) + \
        struct.pack("<2I", o[13], len(opp)) + \
        struct.pack("<2I", pos, len(crcs)) + struct.pack(f"<{len(crcs)}I", *crcs)
    assert len(head) <= 0x100, f"file too large for the block CRCs in the header ({len(crcs)} blocks)"
    blob[:len(head)] = head
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(blob)
    print(f"{out}: {len(blob)} bytes ({len(crcs)} blocks of 256 KB; limit 28): MLB {len(mlb)} + {CARS} car libraries{' and MISC.MLB' if not room else ''} "
          f"({sum(nrec)} materials) + windows' picture and see-through block {len(mlbsec) - env_at}, UNR {len(unr)}, {len(pl)} placements ({nground} drivable), {len(dirs)} road points, "
          f"{len(walls)} walls, {CARS} car models {[tuple(round(v) for v in struct.unpack_from('<4f', models, 24 * k)) for k in range(CARS)]}, "
          f"start {sx_},{sy_} yaw {sa:.3f}, far plane {far}, background colour {colour}")


main()
