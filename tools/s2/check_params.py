"""Independent reading of Screamer 2's CAR_PARAMS and TRACK_DATA for tools/s2/check_params.sh.

Reads DATA/GAME.DAT (CARS.DTR) and DATA/MAPn.DAT (MAPnDAT.DTR) of the unzipped game with tools/datfile.py
and the .DTR text format (words d.. f.. x.., `*` comments, a section up to END_LIST) and prints every field
of the 20 cars and of the 7 tracks in the exact text that tools/s2/check_params.c prints for the firmware's
parser (s2params.c): floats as the 32-bit value in %.9g. Nothing is written but the text on stdout.

Usage: python -I tools/s2/check_params.py <game folder>
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile  # noqa: E402


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def section(text, name):
    """The words of the section `name` as ('f', float32) / ('d', int) / ('x', int)."""
    lines = text.replace("\r", "").split("\n")
    at = next(i for i, l in enumerate(lines) if l.strip() == name)
    out = []
    for line in lines[at + 1:]:
        line = re.sub(r"\*.*$", "", line)
        for tok in line.split():
            if tok == "END_LIST":
                return out
            kind, val = tok[0], tok[1:]
            if kind == "f":
                out.append(("f", f32(float(val))))
            elif kind == "d":
                out.append(("d", int(val, 10)))
            elif kind == "x":
                out.append(("x", int(val, 16)))
    raise ValueError(name + ": no END_LIST")


def g(v):
    return "%.9g" % v[1] if v[0] == "f" else "%d" % v[1]


def main():
    sys.stdout.reconfigure(newline="\n")        # the same line ends as the C program's
    game = sys.argv[1]
    dat = datfile.read(os.path.join(game, "DATA", "GAME.DAT"))
    w = section(datfile.extract(dat, "CARS.DTR").decode("latin-1"), "CAR_PARAMS")
    assert len(w) == 20 * 22, len(w)
    for i in range(20):
        r = [g(v) for v in w[22 * i:22 * i + 22]]
        print("car %d.%d speed=%s rpm=%s auto=%s gears=%s steer=%s accel=%s grip=%s wheelgrip=%s,%s,%s,%s "
              "traction=%s,%s,%s,%s drittizer=%s brake=%s susp=%s,%s,%s cam=%s,%s"
              % ((i // 5, i % 5) + tuple(r[:2]) + tuple(r[2:4]) + tuple(r[4:7]) + tuple(r[7:11]) + tuple(r[11:15])
                 + tuple(r[15:17]) + tuple(r[17:20]) + tuple(r[20:22])))
    for n in range(1, 8):
        dat = datfile.read(os.path.join(game, "DATA", "MAP%d.DAT" % n))
        w = section(datfile.extract(dat, "MAP%dDAT.DTR" % n).decode("latin-1"), "TRACK_DATA")
        assert len(w) == 68, len(w)
        r = [g(v) for v in w]
        print("track %d far=%s background=%s,%s,%s entry=%s asmoke=%s,%s,%s a2smoke=%s,%s,%s pressure=%s,%s,%s"
              % ((n,) + tuple(r[:14])))
        for k in range(6):
            q = r[14 + 9 * k:23 + 9 * k]
            print("track %d kind %d id=%s grip=%s,%s,%s rough=%s smoke=%s,%s,%s,%s" % ((n, k) + tuple(q)))


main()
