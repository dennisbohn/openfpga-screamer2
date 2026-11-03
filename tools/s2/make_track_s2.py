"""EXPERIMENT (docs/screamer2/comparison.md, task 2): a track image (firmware/track.h, "SCRK"
version 6) from a Screamer 2 track, through tools/make_track.py UNCHANGED. Not part of the
firmware's path; the image stays under build/ (game data, never committed).

The adapters, each marked ADAPTER below, are what Screamer 2's files need beyond the names:
  - file names: GFX/MAPn{L,H}{0,1}1.DAT (L/H = detail set, 0/1 = day/night look, 1 = 256 colours)
    and DATA/MAPn.DAT (S1: GFX/MAPnH.DAT, DATA/MAPn.DAT)
  - MAPnOPP.SPL is ONE spline (S1: ten, the tenth is the player's): repeated ten times
  - no MAPn.MRK (markers): none
  - CARS.DTR / MAPnA.DTR have another layout: stand-ins (one car row, no opponents, TRACK_DATA's
    background colour from S2's "background 256")
  - cars: AUTO1..5.3DX of GFX/CARS101.DAT (team 1, day, 256 colours); S1: AUTO00..06 in the track's file
  - the race screen (GAME.DAT): left empty (a track picture does not draw it)

Usage: python tools/s2/make_track_s2.py <Screamer 2 folder> <track 1..7> <out.bin> [H|L] [look 0|1]
  S2START_ROAD=k in the environment: the player's start place is road point k (MMP array 2) instead of
  the grid's (a chosen frame for the simulation)
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
placements, ground_index, cell_arrays, car_model, grid_pose, pcx, player_path, splines = (
    ns[k] for k in ("placements", "ground_index", "cell_arrays", "car_model", "grid_pose", "pcx", "player_path", "splines"))
BLOCK = ns["BLOCK"]


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


def main():
    global grid_pose
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
    spl1 = datfile.extract(d, track + "OPP.SPL")
    spl = spl1 * 10                                                    # ADAPTER: one spline, ten times
    dtr = datfile.extract(d, track + "DAT.DTR")                        # ADAPTER: MAPnDAT.DTR
    tdata = s2_track_data(dtr)
    far, colour = tdata[0], tdata[4]
    pal = b"".join(struct.pack("<I", col[3 * i] * 4 << 16 | col[3 * i + 1] * 4 << 8 | col[3 * i + 2] * 4) for i in range(256))
    pl, run0 = placements(mmp)
    path = player_path(spl)
    ground, nground = ground_index(pl, run0)
    mrk = b""                                                          # ADAPTER: no markers
    cars = struct.pack("<12si4f2i7f", b"S2 STANDIN", 0, 150.0, 9000.0, 1.0, 1.0, 1, 4, 0, 3.0, 2.0, 1.5, 1.2, 1.0, 0.8)
    cg = datfile.read(os.path.join(root, "GFX", f"CARS1{look}1.DAT"))  # ADAPTER: team 1's cars
    x3d = [datfile.extract(cg, f"AUTO{k}.3DX") for k in range(1, 6)]
    models, files, at = b"", b"", 24 * len(x3d)
    for f in x3d:
        models += car_model(f) + struct.pack("<2I", at + len(files), len(f))
        files += f + b"\0" * (-len(f) % 256)
    models += files
    dirs, walls = cell_arrays(mmp)
    sr = os.environ.get("S2START_ROAD")                                 # EXPERIMENT: the player starts on road point k, to put a
    if sr:                                                              # chosen frame in the simulation (docs/screamer2/performance.md)
        sa, sx_, sy_ = struct.unpack("<f2i", dirs[int(sr)])
        grid_pose = lambda spl_, mmp_: struct.pack("<2if", sx_, sy_, sa)
    car = os.environ.get("S2CAR")                                       # "CARS101:AUTO3": the car alone (see below)
    if car:
        # EXPERIMENT 2b: one S2 car through the track renderer: its .MLB as the track's, its .3DX as the
        # only mesh (object 0, placed at the origin), its .COL as the palette; the rest as the track's
        cname, aname = car.split(":")
        cgd = datfile.read(os.path.join(root, "GFX", cname + ".DAT"))
        mlb = datfile.extract(cgd, aname + ".MLB")
        colc = datfile.extract(cgd, aname + ".COL")
        pal = b"".join(struct.pack("<I", colc[3 * i] * 4 << 16 | colc[3 * i + 1] * 4 << 8 | colc[3 * i + 2] * 4) for i in range(256))
        x3 = datfile.extract(cgd, aname + ".3DX")
        unr = struct.pack("<2I", 8 + 4 + len(x3), 1) + struct.pack("<i", 0) + x3
        pl, run0 = [(0, 0, 0, 0, 0)], [False]
        ground, nground = ground_index(pl, run0)
        dirs, walls = [], []
        colour = min(range(120), key=lambda i: sum((colc[3 * i + k] * 4 - 90) ** 2 for k in range(3)))   # a dark grey
    hud = struct.pack("<2I", 0, 0) + bytes(16) + bytes(26 * 32)        # ADAPTER: no race screen
    names = {e.name.upper() for e in datfile.entries(g)}
    if track + ".BCK" in names and not car:
        bw, bh, back = pcx(datfile.extract(g, track + ".BCK"))
        assert (bw, bh) == (512, 256), f"backdrop {bw} x {bh}"
    else:                                                               # track 7 is indoors: no backdrop
        bw, bh, back = 512, 256, bytes([colour]) * (512 * 256)
    back += back[-512:]
    opp = struct.pack("<2I", 0, 0)                                      # ADAPTER: no opponents

    sections = [mlb, unr, pal, b"".join(struct.pack("<5i", *p) for p in pl),
                b"".join(struct.pack("<3i", *p) for p in path), ground, mrk[4:4],
                cars, models, b"".join(dirs), b"".join(walls), hud, back, opp]
    counts = [len(mlb), len(unr), 0, len(pl), len(path), nground, 0, 1, 5, len(dirs), len(walls), len(hud)]
    offs, pos = [], 0x100
    for s in sections:
        offs.append(pos)
        pos = (pos + len(s) + 255) & ~255
    blob = bytearray(pos)
    for o, s in zip(offs, sections):
        blob[o:o + len(s)] = s
    crcs = [zlib.crc32(blob[o:o + BLOCK]) for o in range(0x100, pos, BLOCK)]
    head = b"SCRK" + struct.pack("<I", 6) + struct.pack("<2I", offs[0], counts[0]) + \
        struct.pack("<2I", offs[1], counts[1]) + struct.pack("<I", offs[2]) + \
        b"".join(struct.pack("<2I", offs[k], counts[k]) for k in range(3, 11)) + \
        grid_pose(spl, mmp) + struct.pack("<2I", offs[11], counts[11]) + \
        struct.pack("<2i", 0, 0) + struct.pack("<2I", offs[12], colour) + \
        struct.pack("<2I", offs[13], len(opp)) + \
        struct.pack("<2I", pos, len(crcs)) + struct.pack(f"<{len(crcs)}I", *crcs)
    assert len(head) <= 0x100, f"file too large for the block CRCs in the header ({len(crcs)} blocks)"
    blob[:len(head)] = head
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(blob)
    sx = struct.unpack_from("<2if", blob, 0x5C)
    print(f"{out}: {len(blob)} bytes ({len(crcs)} blocks of 256 KB; limit 28): MLB {len(mlb)}, UNR {len(unr)}, "
          f"{len(pl)} placements ({nground} drivable), {len(path)} path points, {len(dirs)} road points, {len(walls)} walls, "
          f"{len(x3d)} car models {[tuple(round(v) for v in struct.unpack_from('<4f', models, 24 * k)) for k in range(5)]}, "
          f"start {sx[0]},{sx[1]} yaw {sx[2]:.3f}, far plane {far}, background colour {colour}")


main()
