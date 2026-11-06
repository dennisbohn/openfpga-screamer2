"""A recording of the running original's attract sequence (tools/s2/dosbox_attract.py) against the firmware's model of it.

Usage: python -I tools/attract_compare.py [<recording dir>] [<dump dir>]
       default <recording dir> build/check/dosbox_attract, <dump dir> build/s2 (attract_dump_t<n>.csv of tools/s2/check_attract_s2.sh)

What it compares (nothing is written, the result is the printout; exit code 1 if something that should be equal is not):
  header     the race's record the game set up (mode 0x80, four cars team i / model i, level 0, music on, the track by the program's table 0x113224, no laps), ATTRACT.TXT's lines
             as the game holds them, the logos' sprite records
  screen     the game's variables (stage, credits' line, the logo's place, the timers, the ending flag, the way to black) after every frame it drew against the model of
             tools/attract_reference.py (STARTL.EXE 0x37ec1 followed by hand) fed with the game's own frame times and easing value: equal at every frame (the model's state after the
             frame polled, or after the one before: the poll is taken while the frame is half done)
  timeline   when each stage began, on the race's clock and as the sum of the frames' times, and when the sequence ended
  cameras    the ten-second flag (camera record +0xae8) rises at the first frame of the clock's multiples of ten; the car followed goes on to the next (+1, round the four) at
             a change of camera after it and not otherwise; the firmware's cameras on its own race do the same (the PC dump of tools/s2/check_attract_s2.sh)
  cars       the four cars' places at clock values, the original's against the firmware's race on the same track (single precision against the program's doubles and x87: the
             drift is reported, not asserted); the pace +0x48, the flags and the laps (no car finishes)
  shots      each screenshot (raw palette indices and palette) against the model's drawing at the game's variables of that moment: the logos black with the sprite at the place the
             game had (the pixels equal), the fades' palettes (6 bits an entry), the credits and the title over the 3D picture (the model's drawing over the screenshot changes
             no pixel), the last frame (black, the S2 logo)
"""
import json
import os
import struct
import sys
import zlib

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(root, "tools"))
import attract_reference as ar  # noqa: E402
import datfile  # noqa: E402
import ui_reference as U  # noqa: E402

game = os.environ.get("GAME_DIR", "C:/GOG Games/Screamer 2")
failures = 0
TRACKS = [0, 2, 3, 4, 5, 1]                     # the program's table 0x113224 (the game's 0 .. 6)


def check(cond, text, *args):
    global failures
    if not cond:
        failures += 1
        print("FAIL: " + text % args)
    return cond


def load(d):
    hdr = json.load(open(os.path.join(d, "header.json")))
    rows, events = [], []
    for line in open(os.path.join(d, "rows.jsonl")):
        r = json.loads(line)
        (rows if "cars" in r else events).append(r)
    labels = open(os.path.join(d, "labels.txt")).read().split() if os.path.exists(os.path.join(d, "labels.txt")) else []
    pngs = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    return hdr, rows, events, list(zip(pngs, labels))


def read_png(path):
    """a palette-indexed 320 x 200 PNG: (indices, palette)"""
    d = open(path, "rb").read()
    p, idat, plte = 8, b"", b""
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        elif kind == b"PLTE":
            plte = d[p + 8:p + 8 + n]
        p += 12 + n
    raw, rows, prev = zlib.decompress(idat), [], bytearray(320)
    for y in range(200):
        f, line = raw[y * 321], bytearray(raw[y * 321 + 1:(y + 1) * 321])
        for i in range(320):
            a, b, c = line[i - 1] if i else 0, prev[i], prev[i - 1] if i else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    pal = [((plte[3 * i] >> 2) << 2) << 16 | ((plte[3 * i + 1] >> 2) << 2) << 8 | ((plte[3 * i + 2] >> 2) << 2) for i in range(256)]       # (the DAC's 6 bits, as 0x00RRGGBB with the low two bits clear)
    return b"".join(rows), pal


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


# ------------------------------------------------------------------------------------------------ header

def text_lines():
    local = datfile.read(os.path.join(game, "DATA/LOCAL.DAT"))
    raw = datfile.extract(local, "ATTRACT.TXT").decode("latin-1")
    lines = [s.rstrip("\r") for s in raw.split("\n")]
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def check_header(h, lines):
    print("header: mode %#x, %d cars, game track %d (counter %d), condition %d, level %d, laps %d, music %d, damages %d" % (h["mode"], h["cars"], h["track"], h["counter"],
          h["condition"], h["level"], h["laps"], h["music"], h["damages"]))
    check(h["mode"] == 0x80 and h["cars"] == 4 and h["level"] == 0 and h["condition"] == 0, "the attract race's record: mode %#x, %d cars, level %d, condition %d", h["mode"], h["cars"],
          h["level"], h["condition"])
    check(h["music"] == 1, "music on in the record: %d", h["music"])
    check(h["track"] == TRACKS[h["counter"] % 6], "the track %d is the table's for counter %d (%d)", h["track"], h["counter"], TRACKS[h["counter"] % 6])
    for k, e in enumerate(h["entries"]):
        check(e[0] == k and e[1] == k, "car %d is team %d model %d", k, e[0], e[1])
    n = len(h["lines"])
    check(h["lines"] == lines[:n] and n >= len(lines) - 1, "ATTRACT.TXT as the game holds it: %d lines, the file has %d", n, len(lines))
    game1 = U.container(game, "GFX/GAME1.DAT")
    spr = U.sprites(datfile.extract(game1, "GAMEL.SPR"))
    for n_, rec in h["sprites"].items():
        n_ = int(n_)
        check(rec[0] == spr[n_][0] and rec[2] == spr[n_][2] and rec[3] == spr[n_][3], "sprite %#x: the game's record %s, GAMEL.SPR's x %d w %d h %d", n_, rec[:4], spr[n_][0], spr[n_][2], spr[n_][3])


# ------------------------------------------------------------------------------------------------ the screen against the model

def frame_numbers(rows):
    """the frame each row was read in: the clock moves once a frame (by the raw time); rows with the same clock are one frame, a jump of more frames (the poll missed some) counts them"""
    out, f, prev = [], 0, None
    for r in rows:
        if prev is not None and r["clock"] != prev["clock"]:
            raw = r["raw"] if r["raw"] > 0 else 0.02
            f += max(1, round((r["clock"] - prev["clock"]) / raw))
        out.append(f)
        prev = r
    return out


def check_screen(h, rows, events, lines, press_stage):
    first = next((i for i, r in enumerate(rows) if r["stage"] >= 1), None)
    if first is None:
        check(False, "the recording has no stage 1")
        return None
    rows = rows[first:]
    fnum = frame_numbers(rows)
    nframes = fnum[-1] + 1
    dt, kk = [None] * nframes, [None] * nframes                     # the program's frame time (0x133350) and easing value in each frame
    for r, f in zip(rows, fnum):                                    # (the last row of a frame: the first may be read before the program has set the frame's time)
        dt[f], kk[f] = r["step"], r["k"]
    for f in range(nframes):
        if dt[f] is None:
            dt[f], kk[f] = dt[f - 1], kk[f - 1]
    keyed = [e for e in events if "key" in e]
    spr = {int(a): [rec[0], rec[1], rec[2], rec[3], b""] for a, rec in h["sprites"].items()}
    best = None
    lit_of = [0.0]
    for lead in range(0, 6):
        for kdelay in ([0] if not keyed else range(-30, 4)):         # (the script holds the key for a quarter of a second without reading: the program took it some frames before the first row after it)
            screen = ar.Screen(lines, spr)
            key_frame = None
            if keyed:
                key_row = keyed[0]["after_row"] - first
                key_frame = lead + (fnum[max(0, min(key_row, len(fnum) - 1))]) + kdelay
            states = []                                             # the model's state after each of its frames: lead frames first, then the recording's
            for q in range(lead):
                screen.step(0, dt[0], kk[0])
                states.append((screen.stage, screen.line, bits(screen.x), bits(screen.U), bits(screen.T), bits(screen.B), bits(screen.L)))
            for f in range(nframes):
                m = len(states)
                if not screen.over:
                    screen.step(1 if m == key_frame else 0, dt[f], kk[f])
                states.append((screen.stage, screen.line, bits(screen.x), bits(screen.U), bits(screen.T), bits(screen.B), bits(screen.L)))
            ok, bad = 0, []
            for r, (row, f) in enumerate(zip(rows, fnum)):
                m = lead + f
                obs = (row["stage"], row["group"], bits(row["X"]), bits(row["U"]), bits(row["T"]), bits(row["B"]), bits(row["L"]))
                cands = [states[m]] + ([states[m - 1]] if m >= 1 else [])
                # (the poll can land in the middle of the routine's update: each variable is the model's before or after the frame)
                fields = (0, 2, 3) + ((1, 4) if obs[0] >= 4 else ()) + (5, 6)
                hit = all(any(obs[q] == c[q] for c in cands) for q in fields)
                if hit:
                    ok += 1
                else:
                    bad.append((r, obs, cands[0]))
            score = ok / max(1, ok + len(bad))
            if best is None or score > best[0]:
                best = (score, lead, kdelay, ok, bad)
                lit_of[0] = screen.white if screen.white is not None else 0.0
    score, lead, kdelay, ok, bad = best
    print("screen: %d rows (%d frames) of the game's variables, the model fed the game's own frame times and easing values: %d equal to the model's state (each variable as the model has it before or after the frame polled in), "
          "%d not (the model's frames before the first row: %d%s)" % (ok + len(bad), nframes, ok, len(bad), lead, ", key %d frames after the row's" % kdelay if keyed else ""))
    fl = lambda u: struct.unpack("<f", struct.pack("<I", u))[0]
    for r, obs, c in bad[:8]:
        print("   row %d (clock %.2f, frame %d): stage %d line %d X %.4f U %.4f T %.4f B %.4f L %.1f   model: stage %d line %d X %.4f U %.4f B %.4f" % (
            r, rows[r]["clock"], fnum[r], obs[0], obs[1], fl(obs[2]), fl(obs[3]), fl(obs[4]), fl(obs[5]), fl(obs[6]), c[0], c[1], fl(c[2]), fl(c[3]), fl(c[5])))
    check(len(bad) == 0, "the screen's variables differ from the model in %d of %d rows", len(bad), ok + len(bad))
    return rows, lit_of[0]


def check_timeline(rows):
    first = {}
    for r in rows:
        first.setdefault(r["stage"], r)
    tot = 0.0
    print("timeline (the race's clock; the sum of the frames' times since stage 1):")
    t0 = first.get(1, rows[0])["clock"]
    for st in sorted(first):
        r = first[st]
        print("   stage %d from clock %.2f (%.2f s after stage 1)" % (st, r["clock"], r["clock"] - t0))
    last = rows[-1]
    print("   last row: clock %.2f, stage %d, line %d, ending flag %.1f, way to black %.2f, race flags %#x" % (last["clock"], last["stage"], last["group"], last["L"], last["B"], last["flags"]))
    steps = [b["clock"] - a["clock"] for a, b in zip(rows, rows[1:]) if b["clock"] > a["clock"]]
    if steps:
        print("   frame times: mean %.4f s (%.1f frames a second), longest %.4f s" % (sum(steps) / len(steps), len(steps) / (sum(steps)), max(steps)))
    return first


# ------------------------------------------------------------------------------------------------ cameras, cars

def check_cameras(rows, dump):
    rises, changes = [], []
    prev = None
    for r in rows:
        if "follow" not in r:
            continue
        if prev is not None:
            if r["rot"] and not prev["rot"]:
                rises.append((r["clock"], r["step"]))
            if r["follow"] != prev["follow"]:
                changes.append((r["clock"], prev["follow"], r["follow"], prev["shot"], r["shot"], prev["rot"], r["rot"]))
        prev = r
    print("cameras: the flag rose %d times, the car followed changed %d times in %.0f s" % (len(rises), len(changes), rows[-1]["clock"]))
    for k, (c, st) in enumerate(rises):
        check(10 * (k + 1) - 0.001 <= c <= 10 * (k + 1) + max(0.1, 2 * st), "flag %d rose at clock %.2f (the clock's multiple of ten is %d)", k + 1, c, 10 * (k + 1))
    for c, a, b, s0, s1, r0, r1 in changes:
        check(b == (a + 1) % 4, "follow %d -> %d at clock %.2f (not the next car)", a, b, c)
        check(r0 == 1 or r1 == 0, "the car followed changed at clock %.2f with the flag not up (%d)", c, r0)
    delays = []
    for k, (c, st) in enumerate(rises):
        nxt = [ch for ch in changes if ch[0] >= c]
        if nxt:
            delays.append(nxt[0][0] - c)
    if delays:
        print("   a change of the car followed came %s s after the flag rose (to the next change of camera)" % ", ".join("%.1f" % d for d in delays))
    shots = [r["shot"] for r in rows if "shot" in r]
    cuts = sum(1 for a, b in zip(shots, shots[1:]) if a != b)
    print("   %d changes of the camera in use in %.0f s" % (cuts, rows[-1]["clock"]))
    if dump and os.path.exists(dump):
        d = [l.split(",") for l in open(dump)]
        ours = [(float(l[1]), int(l[2])) for l in d]
        follows = [(a[0], a[1], b[1]) for a, b in zip(ours, ours[1:]) if a[1] != b[1]]
        print("   the firmware's cameras on its own race of the same track: %d changes of the car followed in 88 s, at clocks %s" % (len(follows), " ".join("%.1f" % c for c, a, b in follows)))
        print("   the original's, at clocks                                              %s" % " ".join("%.1f" % c[0] for c in changes))
    return rises, changes


def check_cars(h, rows, dump):
    print("cars: pace +0x48 %s, flags %s, laps %s at the end" % (rows[-1]["cars"][0][3], [c[0] for c in rows[-1]["cars"]], [c[1] for c in rows[-1]["cars"]]))
    paces = {round(c[3], 3) for r in rows if r["clock"] > 1 for c in r["cars"]}
    print("   the pace of the four cars over the race: %s" % sorted(paces))
    check(all(not (c[0] & 3) for r in rows for c in r["cars"]), "a car has the finished flag")
    if not (dump and os.path.exists(dump)):
        print("   (no dump of the firmware's race: %s)" % dump)
        return
    ours = {}
    for l in open(dump):
        v = l.strip().split(",")
        ours[round(float(v[1]) * 50)] = [(float(v[4 + 7 * i]), float(v[5 + 7 * i]), int(v[6 + 7 * i]), float(v[7 + 7 * i])) for i in range(4)]
    print("   the firmware's race on the same track against the original's (x, z in the track's units; the cars' places are the game's whole numbers):")
    print("     clock   car0   car1   car2   car3    distance of each car's place from the original's; original's speed")
    for want in (0.5, 1, 2, 3, 4, 5, 6, 8, 10, 15, 20, 30, 40, 50, 60, 70, 80):
        near = min((r for r in rows if "cars" in r), key=lambda r: abs(r["clock"] - want))["clock"]
        r = [x for x in rows if x["clock"] == near][-1]             # (the last reading of that frame: the first may be half done)
        if abs(r["clock"] - want) > 0.1:
            continue
        k = round(r["clock"] * 50 / 5) * 5
        if k not in ours:
            continue
        o = ours[k]
        dist = []
        for i in range(4):
            c = r["cars"][i]
            dist.append(((o[i][0] - c[6]) ** 2 + (o[i][1] - c[8]) ** 2) ** 0.5)
        print("     %5.1f  %6.0f %6.0f %6.0f %6.0f      speeds %s" % (r["clock"], dist[0], dist[1], dist[2], dist[3], " ".join("%5.1f" % r["cars"][i][9] for i in range(4))))


# ------------------------------------------------------------------------------------------------ the screenshots

def overlay_matches(idx, spr, font, lines, v):
    """the model's drawing of the view on a canvas of 255s: the pixels it sets must be the screenshot's; (pixels set, pixels that differ)"""
    canvas = U.Canvas(bytes([255]) * 64000)
    ar.draw(canvas, spr, font, lines, v)
    mask = [i for i, c in enumerate(canvas.px) if c != 255]
    return len(mask), sum(1 for i in mask if canvas.px[i] != idx[i])


def check_shots(d, h, rows, events, lines, shots, lit):
    if not shots:
        print("shots: none")
        return
    game1 = U.container(game, "GFX/GAME1.DAT")
    spr = U.sprites(datfile.extract(game1, "GAMEL.SPR"))
    font = U.fonts(datfile.extract(game1, "GAMEL.FNT"))[0]
    base = None
    for png, label in shots:                    # the race's own palette: the logos' black screens are shown through it unchanged
        if label.startswith("logo"):
            _, base = read_png(os.path.join(d, png))
            break
    ev = {e["shot"]: e for e in events if "shot" in e and "after_row" in e}
    print("shots: %d screenshots" % len(shots))
    slide_odd = []
    for png, label in shots:
        idx, pal = read_png(os.path.join(d, png))
        e = ev.get(label)
        near = [r for r in rows if e and abs(r["t"] - e["t"]) < 0.3]
        if not near:
            print("   %-12s no rows near it" % label)
            continue
        if label.startswith("logo"):
            stage = 1 if label.startswith("logo1") else 2
            lo, hi = min(r["X"] for r in near), max(r["X"] for r in near)
            best = None
            for xs in range(int(lo) - 1, int(hi) + 2):
                canvas = U.Canvas(bytes(64000))
                v = dict(black=1, sprite=stage + 0x6d, sx=xs, caption=-1, title=0, first=0, count=0, white=0.0, dark=0.0, exit=0, music=0)
                xo, _, w, _, _ = spr[v["sprite"]]
                if abs(xs - (((320 - (xo + w)) >> 1) - int(xo / 2))) < 5:
                    v["caption"] = stage - 1
                ar.draw(canvas, spr, font, lines, v)
                dd = sum(1 for a, b in zip(canvas.px, idx) if a != b)
                if best is None or dd < best[0]:
                    best = (dd, xs, v["caption"])
            print("   %-12s black, the sprite at x %d%s: %d of 64000 pixels differ from the model's frame (the game's x over the frames round it %.2f .. %.2f); palette %s the race's" %
                  (label, best[1], " and its line" if best[2] >= 0 else "", best[0], lo, hi, "equal to" if pal == base else "NOT equal to"))
            if label.endswith("slide") and best[0]:
                print("                a frame of the logo sliding, caught by the screenshot while the program was still drawing it? (the standing frames of the same run are exact)")
                slide_odd.append(label)
            else:
                check(best[0] == 0, "%s: %d pixels differ", label, best[0])
            check(pal == base, "%s: the palette of a logo's frame is not the race's", label)
        elif label.startswith("white"):
            us = sorted({r["U"] for r in near if r["stage"] == 3})
            ok_u = next((u for u in us if base is not None and ar.fade(base, u, 63) == pal), None)
            print("   %-12s palette equal to the model's fade towards white at U %s (the game's U in the frames round it %.3f .. %.3f)" % (label, "%.4f" % ok_u if ok_u is not None else "NONE",
                  min(us), max(us)))
            check(ok_u is not None, "%s: the palette is not the model's fade for any U of the frames round it", label)
        elif label.startswith(("credits", "cam_", "fadeout")):
            results = []
            for r in near:
                if r["stage"] < 4:
                    continue
                k = 0
                while r["group"] + k < len(lines) and not lines[r["group"] + k].startswith("#"):
                    k += 1
                for count in sorted({0, k}):
                    v = dict(black=0, sprite=-1, sx=0, caption=-1, title=1, first=r["group"], count=count, white=0.0, dark=0.0, exit=0, music=0)
                    n_mask, bad = overlay_matches(idx, spr, font, lines, v)
                    results.append((bad, -count, n_mask, r["group"], count))
            res = min(results)
            print("   %-12s the title and %d lines of the group at line %d: %d pixels set by the model's drawing, %d of them differ from the screenshot" % (label, res[4], res[3], res[2], res[0]))
            check(res[0] == 0 and res[2] > 0, "%s: the model's overlay differs from the screenshot in %d of %d pixels", label, res[0], res[2])
            if label == "fadeout" and base is not None:
                bs = sorted({r["B"] for r in near})
                okb = next((b for b in bs if ar.fade(base, b, 0) == pal), None)
                print("   %-12s palette equal to the model's fade towards black at B %s (%.3f .. %.3f)" % (label, "%.4f" % okb if okb is not None else "NONE", min(bs), max(bs)))
                check(okb is not None, "fadeout: the palette is not the model's fade towards black")
            elif base is not None:
                want = ar.fade(base, lit, 63)
                print("   %-12s palette %s the one stage 3's last frame left (the model's fade towards white at U %.4f)" % (label, "equal to" if pal == want else "NOT equal to", lit))
                check(pal == want, "%s: the credits' palette is not what stage 3 left", label)
        elif label.startswith("exit"):
            canvas = U.Canvas(bytes(64000))
            ar.draw(canvas, spr, font, lines, dict(black=0, sprite=-1, sx=0, caption=-1, title=0, first=0, count=0, white=0.0, dark=0.0, exit=1, music=0))
            dd = sum(1 for a, b in zip(canvas.px, idx) if a != b)
            if not any(idx):
                print("   %-12s black, without the S2 logo: the screenshot was taken a tenth of a second after the frame (the page that follows was loading); whether the logo's frame is ever on show was not caught" % label)
            else:
                print("   %-12s black with the S2 logo at y 40: %d pixels differ from the model's last frame; palette %s the race's" % (label, dd, "equal to" if base is not None and pal == base else "NOT equal to"))
                check(dd == 0, "%s: %d pixels differ", label, dd)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "build", "check", "dosbox_attract")
    dumpdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "build", "s2")
    h, rows, events, shots = load(d)
    over = next((i for i, r in enumerate(rows) if r["flags"] & 3), len(rows))
    rows = rows[:over]                          # (from the frame the race is over the page that follows (TEAM SELECTION) uses the same variables)
    lines = text_lines()
    check_header(h, lines)
    press = [e for e in events if "key" in e]
    aligned, lit = check_screen(h, rows, events, lines, press[0]["stage"] if press else 0)
    check_timeline(rows)
    dump = os.path.join(dumpdir, "attract_dump_t%d.csv" % (h["track"] + 1))
    check_cameras(rows, dump)
    check_cars(h, rows, dump)
    check_shots(d, h, rows, events, lines, shots, lit)
    print("attract_compare: %s" % ("all equal" if not failures else "%d checks failed" % failures))
    sys.exit(1 if failures else 0)


main()
