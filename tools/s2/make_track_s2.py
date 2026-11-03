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
  - one stand-in row for CARS_DATA (the cars' physics is new in Screamer 2)
  - TRACK_DATA of MAPnDAT.DTR: the far plane (header 0x70) and the background palette entry (0x7C)
  - cars: AUTO1..5 of GFX/CARS1{look}1.DAT (team 1). Their material libraries are not in the track's: each
    car's .MLB is appended after the track's, its material records (16 bytes) go into a section in front
    of the track's MLB (the geometry unit reads a face's material at a signed 16-bit offset from the MLB's
    start, so they have to lie within 32 KB of it, below), and the cars' faces point there
    (docs/screamer2/comparison.md, "Cars' libraries")

Image layout, version 7 (the version 6 of tools/make_track.py with the car materials in front):
  0x100  car materials (the records of AUTO1..5.MLB, texture records' offsets moved to the appended libraries)
         then the track's MAPn.MLB, padded to 256 bytes, then AUTO1.MLB .. AUTO5.MLB each padded to 256;
         the header's MLB offset (0x08) is the MLB, its size (0x0C) all of it
  then   UNR, palette, placements, camera path (empty), ground index, markers (empty), cars (one row),
         car models (5 .3DX, faces moved onto the car materials), road points, walls, race screen
         (empty), backdrop, opponents (empty); header 0x5C the start (road point 0), 0x70 the far plane
         (positive units), 0x74 0, 0x7C the background colour, as tools/make_track.py otherwise.

Usage: python tools/s2/make_track_s2.py <Screamer 2 folder> <track 1..7> <out.bin> [H|L] [look 0|1]
  S2START_ROAD=k in the environment: the player's start place is road point k (MMP array 2) instead of
  point 0 (a chosen frame for the simulation)
"""
import os
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
tools = os.path.join(here, "..")
sys.path.insert(0, tools)
import datfile

# tools/make_track.py runs main() when imported; take everything above that call
src = open(os.path.join(tools, "make_track.py")).read()
ns = {"__file__": os.path.join(tools, "make_track.py"), "__name__": "make_track"}
exec(compile(src.rsplit("\nmain()", 1)[0], "make_track.py", "exec"), ns)
placements, ground_index, cell_arrays, car_model, pcx = (
    ns[k] for k in ("placements", "ground_index", "cell_arrays", "car_model", "pcx"))
BLOCK = ns["BLOCK"]
FIRST = 0x100
CARS = 5                                    # AUTO1..5 of a team
i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
i16 = lambda b, o: struct.unpack_from("<h", b, o)[0]
pad256 = lambda n: (n + 255) & ~255


def s2_track_data(dtr):
    """ADAPTER: Screamer 2's TRACK_DATA is a list of 'd' numbers with comments: far plane (negative),
    background rgb, background 256 (palette entry), asmoke rgb, a2smoke rgb, tyre pressure"""
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
        if len(nums) >= 5:
            break
    return nums


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


def car_library(mlb, at):
    """A car's .MLB placed at offset `at` from the merged MLB's start: (number of material records, the
    records with their texture records' offsets moved, the file with the texture records' pixel offsets
    moved). A texture record is n, one word, then n offsets (one if n < 1); two materials may share one"""
    nrec = mlb_records(mlb)
    out, recs, seen = bytearray(mlb), bytearray(), set()
    for k in range(nrec):
        rec = bytearray(mlb[16 * k:16 * k + 16])
        ro = i32(mlb, 16 * k + 4)
        if ro > 0 and ro + 12 <= len(mlb):
            struct.pack_into("<I", rec, 4, (ro + at) & 0xFFFFFFFF)
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


def main():
    root, n, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    detail = sys.argv[4] if len(sys.argv) > 4 else "H"
    look = sys.argv[5] if len(sys.argv) > 5 else "0"
    gfx = os.path.join(root, "GFX", f"MAP{n}{detail}{look}1.DAT")      # ADAPTER: the name
    g = datfile.read(gfx)
    d = datfile.read(os.path.join(root, "DATA", f"MAP{n}.DAT"))
    track = f"MAP{n}"
    mlb = datfile.extract(g, track + ".MLB")
    unr = datfile.extract(g, track + ".UNR")
    col = datfile.extract(g, track + ".COL")
    mmp = datfile.extract(g, track + ".MMP")
    tdata = s2_track_data(datfile.extract(d, track + "DAT.DTR"))      # ADAPTER: MAPnDAT.DTR
    far, colour = -tdata[0], tdata[4]
    pal = b"".join(struct.pack("<I", col[3 * i] * 4 << 16 | col[3 * i + 1] * 4 << 8 | col[3 * i + 2] * 4) for i in range(256))
    pl, run0 = placements(mmp)
    ground, nground = ground_index(pl, run0)
    dirs, walls = cell_arrays(mmp)
    cars = struct.pack("<12si4f2i7f", b"S2 STANDIN", 0, 150.0, 9000.0, 1.0, 1.0, 1, 4, 0, 3.0, 2.0, 1.5, 1.2, 1.0, 0.8)

    # ADAPTER: team 1's cars, their libraries merged (see the header)
    cg = datfile.read(os.path.join(root, "GFX", f"CARS1{look}1.DAT"))
    car_mlb = [datfile.extract(cg, f"AUTO{k}.MLB") for k in range(1, CARS + 1)]
    x3d = [datfile.extract(cg, f"AUTO{k}.3DX") for k in range(1, CARS + 1)]
    nrec = [mlb_records(m) for m in car_mlb]
    cmat_size = 16 * sum(nrec)
    mlb_off = FIRST + pad256(cmat_size)
    assert mlb_off - FIRST <= 32768, "car materials too far below the MLB"
    lib_at, at = [], pad256(len(mlb))
    for m in car_mlb:
        lib_at.append(at)
        at += pad256(len(m))
    cmat, mlbsec = b"", mlb + bytes(pad256(len(mlb)) - len(mlb))
    for k in range(CARS):
        _, recs, lib = car_library(car_mlb[k], lib_at[k])
        cmat += recs
        mlbsec += lib + bytes(pad256(len(lib)) - len(lib))
    models, files, at = b"", b"", 24 * CARS
    for k in range(CARS):
        delta = FIRST + 16 * sum(nrec[:k]) - mlb_off
        f = car_faces_onto(x3d[k], delta)
        models += car_model(f) + struct.pack("<2I", at + len(files), len(f))
        files += f + b"\0" * (-len(f) % 256)
    models += files

    sr = os.environ.get("S2START_ROAD")                                 # a chosen start for the simulation
    sa, sx_, sy_ = struct.unpack("<f2i", dirs[int(sr) if sr else 0])
    names = {e.name.upper() for e in datfile.entries(g)}
    if track + ".BCK" in names:
        bw, bh, back = pcx(datfile.extract(g, track + ".BCK"))
        assert (bw, bh) == (512, 256), f"backdrop {bw} x {bh}"
    else:                                                               # track 7 is indoors: no backdrop
        back = bytes([colour]) * (512 * 256)
    back += back[-512:]
    hud = bytes(8 + 16 + 26 * 32)                                       # ADAPTER: no race screen (0 images, 0 fonts)
    opp = bytes(8)                                                      # ADAPTER: no opponents

    sections = [cmat, mlbsec, unr, pal, b"".join(struct.pack("<5i", *p) for p in pl), b"", ground, b"",
                cars, models, b"".join(dirs), b"".join(walls), hud, back, opp]
    counts = [0, len(mlbsec), len(unr), 0, len(pl), 0, nground, 0, 1, CARS, len(dirs), len(walls), len(hud)]
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
        struct.pack("<2i", far, 0) + struct.pack("<2I", o[12], colour) + \
        struct.pack("<2I", o[13], len(opp)) + \
        struct.pack("<2I", pos, len(crcs)) + struct.pack(f"<{len(crcs)}I", *crcs)
    assert len(head) <= 0x100, f"file too large for the block CRCs in the header ({len(crcs)} blocks)"
    blob[:len(head)] = head
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(blob)
    print(f"{out}: {len(blob)} bytes ({len(crcs)} blocks of 256 KB; limit 28): MLB {len(mlb)} + {CARS} car libraries "
          f"({sum(nrec)} materials), UNR {len(unr)}, {len(pl)} placements ({nground} drivable), {len(dirs)} road points, "
          f"{len(walls)} walls, {CARS} car models {[tuple(round(v) for v in struct.unpack_from('<4f', models, 24 * k)) for k in range(CARS)]}, "
          f"start {sx_},{sy_} yaw {sa:.3f}, far plane {far}, background colour {colour}")


main()
