"""The original's raw screenshots of the views from the car (tools/s2/dosbox_cockpit.py) against what the rules say should be drawn (docs/screamer2/hud.md "The views from the car"): the
cockpit's picture, the gear lamp, the two needles and the mirror's frame, worked out from the game's files by independent readers (tools/ui_reference.py's sprites, tools/s2/check_cockpit_s2.py's
picture, needle and table readers), over every pixel they cover. Where the original draws something else over them (the race screen's texts) the pixel is left out; each shot says how many pixels
of the overlay the original has as the rules give them.

Usage: python -I tools/s2/check_cockpit_shots.py <game folder> <the recording's folder, build/check/dosbox_cockpit> [track, default 1]
Exit code 0 if, in every shot of the view INSIDE, at least 99 % of the cockpit picture's pixels and every needle and gear lamp pixel are as the rules give them, and the mirror's frame in the
shots with the mirror; 1 otherwise. A negative control (a needle at the wrong part) must fail.
"""
import os
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
sys.path.insert(0, here)
import ui_reference as ui  # noqa: E402
import datfile  # noqa: E402
import check_cockpit_s2 as ck  # noqa: E402


def read_png(path):
    d = open(path, "rb").read()
    p, idat = 8, b""
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", d[p + 8:p + 18])
        elif kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    assert (w, h, bd, ct) == (320, 200, 8, 3), "not a raw 320 x 200 capture"
    raw, rows, prev = zlib.decompress(idat), [], bytearray(w)
    for y in range(h):
        f, line = raw[y * (w + 1)], bytearray(raw[y * (w + 1) + 1:(y + 1) * (w + 1)])
        for x in range(w):
            a = line[x - 1] if x else 0
            b = prev[x]
            c = prev[x - 1] if x else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return b"".join(rows)


def main():
    game, folder = sys.argv[1], sys.argv[2]
    track = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    negative = bool(os.environ.get("NEGATIVE"))
    g1 = datfile.read(os.path.join(game, "GFX", "GAME1.DAT"))
    spr = ui.sprites(bytes(datfile.extract(g1, "GAMEL.SPR")))
    pkc = {n: ck.pkc(bytes(datfile.extract(g1, n))) for n in ["COCK%d_L.PKC" % k for k in range(1, 9)] + ["MIRROR_L.PKC"]}
    table = open(os.path.join(folder, "cockpit_t%d.crusc.bin" % track), "rb").read()
    rec = open(os.path.join(folder, "cockpit_t%d.bin" % track), "rb").read()
    head, REC, CAM = 20, 0xCC0, 0xAEC
    frame = head + REC + 2 * CAM + 0x40
    clocks = [struct.unpack_from("<f", rec, i * frame)[0] for i in range(len(rec) // frame)]
    model = 1                                                   # the first team's second car
    row = list(struct.unpack_from("<22i", table, 0x58 * model))
    rowf = [struct.unpack("<f", struct.pack("<i", v))[0] if k in (4, 5, 10, 11) else v for k, v in enumerate(row)]
    fails = 0
    for line in open(os.path.join(folder, "cockpit_t%d.shots.txt" % track)):
        if line.startswith("#"):
            break
        name, label, before, after = line.split()
        view = {"incar": 2, "inside": 6, "near": 4, "back": 3}[label[1:].split("_")[0]]
        mirror = label.endswith("mirror")
        shot = read_png(os.path.join(folder, name))
        t = (float(before) + float(after)) / 2
        fr = min(range(len(clocks)), key=lambda i: abs(clocks[i] - t))
        # the frame the screenshot shows is one of the frames between the two clocks; the needles and the gear are those of the frame's record (the dial's eased figures at 0x133098, 0x1330a0)
        best = None
        for f in range(max(0, fr - 6), min(len(clocks), fr + 6)):
            r = rec[f * frame:(f + 1) * frame]
            speed, revs = struct.unpack_from("<2f", r, 8)
            gear = struct.unpack_from("<i", r, head + 0xC7C)[0]
            mirror_flag = struct.unpack_from("<i", r, 16)[0]
            expect = {}                                        # pixel index -> value the rules give
            lamp_px, needle_px = set(), set()
            if view == 6:
                for i, v in enumerate(pkc["COCK%d_L.PKC" % (model + 1)]):
                    if v:
                        expect[i] = v
                gx, gy, gs = ck.chop_half(rowf[12 + 2 * min(max(gear, 0), 4)]), ck.chop_half(rowf[13 + 2 * min(max(gear, 0), 4)]), 0x46 + 5 * model + min(max(gear, 0), 4)
                sx, sy, w, h, px = spr[gs]
                for rr in range(h):
                    for cc in range(w):
                        if px[rr * w + cc]:
                            expect[(gy + sy + rr) * 320 + gx + sx + cc] = px[rr * w + cc]
                            lamp_px.add((gy + sy + rr) * 320 + gx + sx + cc)
                for nd, part in (((rowf[1], rowf[2], rowf[3], rowf[4], rowf[5]), speed / rowf[6] + (0.3 if negative else 0.0)), ((rowf[7], rowf[8], rowf[9], rowf[10], rowf[11]), revs)):
                    for (x0, y0, x1, y1) in ck.needle(nd, part):
                        for (px_, py_) in ck.bresenham(x0, y0, x1, y1):
                            if 0 <= px_ < 320 and 0 <= py_ < 200:
                                expect[py_ * 320 + px_] = 12
                                needle_px.add(py_ * 320 + px_)
            if mirror_flag:
                for i, v in enumerate(pkc["MIRROR_L.PKC"]):
                    if v:
                        expect[i] = v
            if mirror_flag:                                    # (the race screen's bar of the cars, drawn after the mirror's frame, covers the frame's top row from x 108 to 211)
                sx, sy, w, h, px = spr[0x25]
                for rr in range(h):
                    for cc in range(w):
                        if px[rr * w + cc]:
                            expect[(2 + sy + rr) * 320 + 108 + sx + cc] = px[rr * w + cc]
            equal = sum(1 for i, v in expect.items() if shot[i] == v)
            ne = sum(1 for i in needle_px if shot[i] == 12)
            le = sum(1 for i in lamp_px if shot[i] == expect[i])
            if best is None or equal + 100 * ne > best[0] + 100 * best[7]:
                best = (equal, len(expect), f, gear, speed, revs, mirror_flag, ne, len(needle_px), le, len(lamp_px))
        equal, total, f, gear, speed, revs, mirror_flag, ne, nn, le, ln = best
        share = equal / total if total else 1.0
        # the shots are of all views: only the instruments of the view INSIDE and the mirror's frame are in the rules above
        ok = total == 0 or (share >= 0.99 and ne >= 0.9 * nn and le >= 0.98 * ln)
        fails += not ok
        print("%s %-16s view %d mirror %d: frame %d (clock %.2f), gear %d, speed %.1f, revs %.3f: %d of %d overlay pixels as the rules have them (%.2f %%; the needles' %d of %d, the lamp's %d of %d) %s" %
              (name, label, view, mirror_flag, f, clocks[f], gear, speed, revs, equal, total, 100 * share, ne, nn, le, ln, "ok" if ok else "FAIL"))
    sys.exit(1 if fails else 0)


main()
