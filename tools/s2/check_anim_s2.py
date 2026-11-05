"""The animated scenery's checks (tools/s2/check_anim_s2.sh): scenarios for the firmware's model, its independent reading in Python (tools/s2/anim_ref.py) and the
running original's own numbers (tools/s2/dosbox_anim.py), compared line by line.

    scenario <game> <track> <scenario.txt> <expected.txt>
        a tour of the player over the track's rows (to the first place of each in turn, three laps, 20,000 frames of 20 ms) as lines "F <clock> <px> <py> <lap>", and the states
        anim_ref.py's reading gives: "F ..." then one "R <row> <shown> <heard> <x> <y> <height> <heading> <climb>" a row
    compare <game> <track> <states_c.txt> <expected.txt> [negative]
        the firmware's states (tools/s2/check_anim_s2.c) against the expected ones: shown and heard equal, the places within 1 (float against double, a truncation on the other
        side), the angles within 1 (2048ths of a turn); exit status 1 on a mismatch. With `negative` the expected states are made by a deliberately wrong rule (the reach of
        a row halved) and the comparison must fail
    dosbox_scenario <folder> <scenario.txt>
        the frames recorded in the running original (dosbox_anim.py) as a scenario
    misc <game> <image> <track>
        the track image's copy of MISC.MLB (the materials of the animated rows' meshes, in front of the MLB after the cars') against the container's own: every material
        record the rows' meshes use (every face of every block at every level of detail, with the offset the builder moves it by) has the routine, the extents and every
        frame's pixels of the original's record
    dosbox <game> <track> <folder> <states_c.txt>
        the firmware's states against the original's rows and object records of the recording (a row is seen at the clock of its frame or of the frames just before it, as the
        recording samples between the game's own steps)
"""
import json
import math
import os
import struct
import sys
import copy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_ref as ar  # noqa: E402


def scenario_frames(rows, paths, frames=20000):
    """a tour: the player goes to the first place of each row in turn at 300 units a frame, three laps (the lap counter -1 for the first 150 frames, the grid's hold, then 0, 1,
    2 each a third of the rest); the clock in 20 ms steps"""
    stops = []
    for r in rows:
        pl = [k[0][1] if k else 0.0 for k in paths[r["path"]]] if r["name"][0] == "@" else ar.path_at(paths[r["path"]], 0.0)[0]
        stops.append((int(pl[0]), int(pl[1])))
    x, y = stops[0] if stops else (0, 0)
    out, k, step = [], 0, 20
    for f in range(frames):
        tx, ty = stops[k % len(stops)] if stops else (0, 0)
        d = math.hypot(tx - x, ty - y)
        if d < 300.0:
            k += 1
        else:
            x += int(300.0 * (tx - x) / d)
            y += int(300.0 * (ty - y) / d)
        lap = -1 if f < 150 else (f - 150) * 3 // (frames - 150)
        out.append(((f + 1) * 0.02, x, y, lap))
    return out


def fmt_f(frame):
    return "F %.6f %d %d %d" % frame


def row_line(i, shown, heard, place, angle):
    return "R %d %d %d %d %d %d %d %d" % (i, shown, heard, place[0], place[1], place[2], angle[0], angle[1])


def expected(rows, paths, frames, wrong=False):
    """the lines of the scenario and of anim_ref.py's states for it (struct.pack of the clock as a float, as the game keeps it)"""
    if wrong:
        ar.ALIVE = 26000
    R = ar.Rows(rows, paths)
    scn, exp = [], []
    for fr in frames:
        clock = struct.unpack("<f", struct.pack("<f", fr[0]))[0]
        R.step(clock, fr[1], fr[2], fr[3])
        scn.append(fmt_f(fr))
        exp.append(fmt_f(fr))
        for i in range(len(rows)):
            exp.append(row_line(i, int(R.shown[i]), int(R.heard[i]), R.place[i], R.angle[i] if R.shown[i] else (0, 0)))
    ar.ALIVE = 52000
    return scn, exp


def parse_states(path):
    """[(frame line, [(row, shown, heard, x, y, h, heading, climb)])]"""
    out, cur = [], None
    for ln in open(path):
        if ln.startswith("F "):
            cur = []
            out.append((ln.strip(), cur))
        elif ln.startswith("R "):
            cur.append(tuple(int(v) for v in ln.split()[1:]))
    return out


def cyc(a, b):
    d = (a - b) & 0x7FF
    return min(d, 2048 - d)


def compare(rows, got, want, flags_free=()):
    """the number of rows (over all frames) whose state differs, and the first few"""
    bad, shown_n, heard_n, n, firsts = 0, 0, 0, 0, []
    for (fl, g), (_, w) in zip(got, want):
        for a, b in zip(g, w):
            n += 1
            i = a[0]
            ok = a[1] == b[1] and a[2] == b[2] and max(abs(a[3] - b[3]), abs(a[4] - b[4]), abs(a[5] - b[5])) <= 1
            if ok and a[1] and not rows[i]["flags"] & 4:
                ok = cyc(a[6], b[6]) <= 1 and cyc(a[7], b[7]) <= 1
            shown_n += a[1]
            heard_n += a[2]
            if not ok:
                bad += 1
                if len(firsts) < 5:
                    firsts.append((fl, a, b))
    return bad, n, shown_n, heard_n, firsts


def dosbox_frames(folder):
    """the recording: [(clock, px, py, lap, rows bytes, [object records])]"""
    out = []
    with open(os.path.join(folder, "frames.jsonl")) as f:
        head = json.loads(f.readline())
        for ln in f:
            j = json.loads(ln)
            if "clock" not in j:
                continue
            clock = struct.unpack("<f", bytes.fromhex(j["clock"]))[0]
            car = bytes.fromhex(j["car"])
            lap, = struct.unpack_from("<i", car, 8)
            px, _, py = struct.unpack_from("<3i", car, 0x38)
            out.append((clock, px, py, lap, bytes.fromhex(j["rows"]), [bytes.fromhex(o) for o in j["objs"]]))
    return head, out


def mlb_records(m):
    """the material records of an .MLB: the table of 16-byte records up to the first texture record (make_track_s2.py's, the PC port's mlb_read)"""
    end, k = len(m), 0
    while (k + 1) * 16 <= end:
        ro = struct.unpack_from("<i", m, 16 * k + 4)[0]
        if ro > 0 and ro + 12 <= len(m) and ro < end:
            end = ro
        k += 1
    return k


def mesh_materials(x):
    """the material offsets (from the mesh's own file) of every face of every block at the three levels of detail"""
    i32 = lambda o: struct.unpack_from("<i", x, o)[0]
    out = set()
    for lvl in range(3):
        for b in range(i32(0x14)):
            blk = i32(0x18 + 4 * (b + 13 * lvl))
            if blk <= 0:
                continue
            for r in range(16):
                ro = i32(blk + 0x28 + 4 * r)
                j = 0
                while ro > 0 and struct.unpack_from("<H", x, blk + ro + 2 * j)[0] != 0xFFFF:
                    fc = blk + struct.unpack_from("<H", x, blk + ro + 2 * j)[0]
                    out.add(struct.unpack_from("<h", x, fc + 4)[0])
                    j += 1
    return out


def check_misc(game, image, track):
    """the report of the `misc` mode: (materials checked, frames checked) or raises"""
    import datfile
    img = open(image, "rb").read()
    mlb_off, = struct.unpack_from("<I", img, 8)
    g = datfile.read(os.path.join(game, "GFX", "MAP%dH01.DAT" % track))
    misc = datfile.extract(g, "MISC.MLB")
    nmisc = mlb_records(misc)
    rec = lambda m, k: struct.unpack_from("<4i", m, 16 * k)
    total = (mlb_off - 0x100) // 16
    start = None
    for i in range(total - nmisc, -1, -1):                      # the last place where MISC's records are (a routine and the extents each)
        if all(rec(img[0x100:], i + k)[0::2][0] == rec(misc, k)[0] and rec(img[0x100:], i + k)[2:] == rec(misc, k)[2:] for k in range(nmisc)):
            start = i
            break
    assert start is not None, "MISC.MLB's records are not in the image"
    delta = 0x100 + 16 * start - mlb_off
    rows, _ = ar.load(game, track)
    mlb = img[mlb_off:]
    names = sorted({r["name"].upper() for r in rows if r["name"][0] != "@"})
    n_mat = n_fr = 0
    for nm in names:
        x = datfile.extract(g, nm + ".3DX")
        for m in sorted(mesh_materials(x)):
            k = m // 16
            assert m % 16 == 0 and 0 <= k < nmisc, (nm, m)
            a, b = rec(misc, k), rec(img[0x100:], start + k)
            assert (a[0], a[2], a[3]) == (b[0], b[2], b[3]), (nm, m, a, b)
            n_mat += 1
            if a[1] <= 0:                                       # a flat colour: no texture
                assert b[1] == a[1], (nm, m)
                continue
            frames = struct.unpack_from("<i", misc, a[1])[0]
            frames = frames & 0xFFFF if frames > 0 else 1
            assert struct.unpack_from("<i", mlb, b[1])[0] == struct.unpack_from("<i", misc, a[1])[0], (nm, m)
            for f in range(frames):
                po = struct.unpack_from("<I", misc, a[1] + 8 + 4 * f)[0]
                pi = struct.unpack_from("<I", mlb, b[1] + 8 + 4 * f)[0]
                for row in range(a[3]):
                    assert mlb[pi + 256 * row:pi + 256 * row + a[2]] == misc[po + 256 * row:po + 256 * row + a[2]], (nm, m, f, row)
                n_fr += 1
    print("track %d: MISC.MLB's %d records in the image at record %d (the faces move by %d), the %d materials of %s with their %d frames equal the container's" %
          (track, nmisc, start, delta, n_mat, " ".join(names), n_fr))


def main():
    cmd = sys.argv[1]
    if cmd == "scenario":
        game, track = sys.argv[2], int(sys.argv[3])
        rows, paths = ar.load(game, track)
        scn, exp = expected(rows, paths, scenario_frames(rows, paths))
        open(sys.argv[4], "w").write("\n".join(scn) + "\n")
        open(sys.argv[5], "w").write("\n".join(exp) + "\n")
    elif cmd == "compare":
        game, track = sys.argv[2], int(sys.argv[3])
        rows, paths = ar.load(game, track)
        got, want = parse_states(sys.argv[4]), parse_states(sys.argv[5])
        negative = len(sys.argv) > 6 and sys.argv[6] == "negative"
        if negative:
            frames = [tuple(float(v) if k == 0 else int(v) for k, v in enumerate(ln.split()[1:])) for ln, _ in got]
            _, exp = expected(rows, paths, frames, wrong=True)
            open(sys.argv[5] + ".wrong", "w").write("\n".join(exp) + "\n")
            want = parse_states(sys.argv[5] + ".wrong")
        bad, n, shown, heard, firsts = compare(rows, got, want)
        print("track %d: %d frames, %d row states (%d shown, %d heard): %d differ%s" % (track, len(got), n, shown, heard, bad, " (the negative control: a wrong reach)" if negative else ""))
        for f in firsts:
            print("   ", f)
        sys.exit(0 if (bad > 0) == negative else 1)
    elif cmd == "misc":
        check_misc(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    elif cmd == "dosbox_scenario":
        _, fr = dosbox_frames(sys.argv[2])
        open(sys.argv[3], "w").write("\n".join("F %.9f %d %d %d" % f[:4] for f in fr) + "\n")
    elif cmd == "dosbox":
        game, track, folder = sys.argv[2], int(sys.argv[3]), sys.argv[4]
        rows, paths = ar.load(game, track)
        head, fr = dosbox_frames(folder)
        got = parse_states(sys.argv[5])
        assert len(got) == len(fr), (len(got), len(fr))
        n_rows = len(rows)
        tot = {"place": 0, "place_ok": 0, "heard": 0, "heard_ok": 0, "angle": 0, "angle_ok": 0, "lagged": 0, "bad": 0}
        worst = 0
        hist = []
        for k, ((clock, px, py, lap, rb, objs), (_, g)) in enumerate(zip(fr, got)):
            hist.append(g)
            hist = hist[-6:]
            for i in range(n_rows):
                own = rows[i]["name"][0] != "@"
                r = rb[0x4C * i:0x4C * (i + 1)]
                gx, gh, gy = struct.unpack_from("<3i", r, 0x30)
                heard = struct.unpack_from("<i", r, 0x48)[0] != 0
                # the sampler sees the game's row after the clock changed and maybe before the game's routine ran: the state of this frame or of one of the last five
                match = None
                for back, hg in enumerate(reversed(hist)):
                    a = hg[i]
                    if max(abs(a[3] - gx), abs(a[4] - gy), abs(a[5] - gh)) <= 1:
                        match = back
                        break
                tot["place"] += 1
                if match is not None:
                    tot["place_ok"] += 1
                    tot["lagged"] += match > 0
                else:
                    tot["bad"] += 1
                    worst = max(worst, max(abs(hist[-1][i][3] - gx), abs(hist[-1][i][4] - gy), abs(hist[-1][i][5] - gh)))
                if rows[i]["sample"]:
                    tot["heard"] += 1
                    tot["heard_ok"] += any(bool(hg[i][2]) == heard for hg in hist[-2:])
                if own and objs[i] and not rows[i]["flags"] & 4:
                    pitch, heading = struct.unpack_from("<2i", objs[i], 0x14)
                    tot["angle"] += 1
                    tot["angle_ok"] += any(hg[i][1] and cyc(hg[i][6], heading) <= 1 and cyc(hg[i][7], pitch) <= 1 for hg in hist)
        print("track %d, the original's %d frames of %s: %s; places equal to the firmware's (this frame's or one of the last five, within 1) %d of %d (%d of them one or more "
              "frames behind), sounds on/off equal %d of %d, headings and climbs of the objects equal %d of %d" %
              (track, len(fr), os.path.basename(folder), "detail %d, animations %s" % (head["detail"], "on" if head["anim_on"] else "OFF"), tot["place_ok"], tot["place"],
               tot["lagged"], tot["heard_ok"], tot["heard"], tot["angle_ok"], tot["angle"]))
        sys.exit(0 if tot["bad"] <= tot["place"] // 100 and tot["heard_ok"] == tot["heard"] and tot["angle_ok"] >= tot["angle"] * 99 // 100 else 1)


if __name__ == "__main__":
    main()
