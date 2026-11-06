"""Independent reading of the cockpits for tools/s2/check_cockpit_s2.sh (firmware/s2cockpit.c, s2dash.c): COCK1_L.PKC .. COCK8_L.PKC and MIRROR_L.PKC of GFX/GAME1.DAT, CRUSCOTTI of
CRUSC.DTR (DATA/GAME.DAT), and the needles and gear lamps of STARTL.EXE's 0x46755 worked out in double precision from the rules in the comments, with tools/datfile.py.

Usage: python -I tools/s2/check_cockpit_s2.py <game folder> <out folder>
Writes into <out folder>: <name>.pkc (the unpacked pictures, for the C test to read), expected.txt (one line per picture: its number of opaque pixels and a hash over their
places and colours; the 8 x 22 words of CRUSCOTTI as the 32-bit values; the lamps), needles.txt (a line per needle and part of the way: the three lines' ends as the rule gives them).
"""
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile  # noqa: E402


def pkc(data):
    """a 320 x 200 picture: an int32 n < 0 skips -n pixels (see-through, 0), n >= 0 is followed by n pixels"""
    pic = bytearray(320 * 200)
    at, p = 0, 0
    while p + 4 <= len(data) and at < 64000:
        n = struct.unpack_from("<i", data, p)[0]
        p += 4
        if n < 0:
            at += -n
        else:
            n2 = min(n, 64000 - at)
            pic[at:at + n2] = data[p:p + n2]
            at += n2
            p += n
    return pic


def dtr(text, name):
    lines = text.replace("\r", "").split("\n")
    at = next(i for i, l in enumerate(lines) if l.strip() == name)
    out = []
    for line in lines[at + 1:]:
        line = re.sub(r"\*.*$", "", line)
        for tok in line.split():
            if tok == "END_LIST":
                return out
            kind, val = tok[0], tok[1:]
            out.append(struct.unpack("<f", struct.pack("<f", float(val)))[0] if kind == "f" else int(val, 16 if kind == "x" else 10))
    raise ValueError("no END_LIST")


def chop_half(v):
    """the low-res program halves the table's whole numbers: sar after the sign (towards zero)"""
    return (v - (v >> 31)) >> 1 if v < 0 else v >> 1


def trunc(x):
    return int(x)  # towards zero, as the game's frndint in chop mode


def needle(n, part):
    """0x46755: n = (x, y, len, a0, a1); angle = a0 - (a0 - a1) part (under 0.001: 0), the tip len / 2 away; a pixel to either side at a right angle"""
    x, y, ln, a0, a1 = n
    px, py = chop_half(x), chop_half(y)
    a = a0 - (a0 - a1) * part
    if abs(a) < 0.001:
        a = 0.0
    tx, ty = trunc(ln * math.cos(a) * 0.5 + px), trunc(py - ln * math.sin(a) * 0.5)
    b = a + math.pi / 2
    s = (trunc(math.cos(b) + px), trunc(py - math.sin(b)))
    b -= math.pi
    u = (trunc(math.cos(b) + px), trunc(py - math.sin(b)))
    return [(px, py, tx, ty), (s[0], s[1], tx, ty), (u[0], u[1], tx, ty)]


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    g1 = datfile.read(os.path.join(game, "GFX", "GAME1.DAT"))
    exp = []
    for name in ["COCK%d_L.PKC" % k for k in range(1, 9)] + ["MIRROR_L.PKC"]:
        raw = bytes(datfile.extract(g1, name))
        open(os.path.join(out, name.lower()), "wb").write(raw)
        pic = pkc(raw)
        h = 0
        n = 0
        for i, v in enumerate(pic):
            if v:
                n += 1
                h = (h + (i + 1) * v) & 0xFFFFFFFF
        exp.append("pic %s %d %d" % (name, n, h))
    gd = datfile.read(os.path.join(game, "DATA", "GAME.DAT"))
    text = bytes(datfile.extract(gd, "CRUSC.DTR")).decode("latin-1")
    open(os.path.join(out, "crusc.dtr"), "w", newline="").write(text)
    w = dtr(text, "CRUSCOTTI")
    assert len(w) == 8 * 22, len(w)
    for m in range(8):
        row = w[22 * m:22 * m + 22]
        exp.append("dash %d " % m + " ".join("%d" % (struct.unpack("<I", struct.pack("<f", v))[0] if isinstance(v, float) else v & 0xFFFFFFFF) for v in row))
    nl = []
    for m in range(8):
        row = w[22 * m:22 * m + 22]
        speed = (row[1], row[2], row[3], row[4], row[5])
        revs = (row[7], row[8], row[9], row[10], row[11])
        for kind, nd, mx in (("speed", speed, row[6]), ("revs", revs, 1)):
            for k in range(0, 101, 5):
                part = k / 100.0
                if kind == "speed":
                    part = part * mx / mx
                ln = needle(nd, part)
                nl.append("needle %d %s %d " % (m, kind, k) + " ".join("%d %d %d %d" % l for l in ln))
        for gear in range(5):
            exp.append("lamp %d %d %d %d %d" % (m, gear, chop_half(row[12 + 2 * gear]), chop_half(row[13 + 2 * gear]), 0x46 + 5 * m + gear))
    open(os.path.join(out, "expected.txt"), "w").write("\n".join(exp) + "\n")
    open(os.path.join(out, "needles.txt"), "w").write("\n".join(nl) + "\n")
    print("%d pictures, 8 dashboards, %d needle positions" % (9, len(nl)))


main()
