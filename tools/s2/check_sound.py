"""Independent reading of Screamer 2's sound tables for tools/s2/check_sound.sh.

Reads DATA/SOUND.DAT (GAME.DTR and the .RAW samples) and DATA/GAME.DAT (MISC.DTR's CD_TRACKS) of the unzipped game
with tools/datfile.py and the .DTR text format (words s<name> x<hex> d<decimal>, `*` comments, a section up to END_LIST)
and prints what tools/s2/check_sound.c prints for the firmware's reading (firmware/s2snd.c): per sample number its
place in the container, length, rate, volume, loop and the CRC-32 of its bytes. Nothing is written but the text on stdout.

Usage: python -I tools/s2/check_sound.py <game folder>
"""
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile  # noqa: E402

SAMPLES = 0x21


def num(tok):
    """x1f, x-1, d-5, d10800 -> int"""
    return int(tok[1:], 16 if tok[0] == "x" else 10)


def main():
    sys.stdout.reconfigure(newline="\n")        # the same line ends as the C program's
    game = sys.argv[1]
    sound = datfile.read(os.path.join(game, "DATA", "SOUND.DAT"))
    ents = {e.name.upper(): e for e in datfile.entries(sound)}
    text = datfile.extract(sound, "GAME.DTR").decode("latin-1").replace("\r", "")
    body = text.split("SOUND_DATA", 1)[1].split("END_LIST", 1)[0]
    toks = re.sub(r"(?m)^\s*\*.*$", "", body).split()
    rows = {}
    while toks:
        name, f = toks[0], toks[1:9]
        assert name[0] == "s" and len(f) == 8, toks[:10]
        toks = toks[9:]
        ident, looplen, loopcnt, vol, rate, bits, chans, fmt = (num(t) for t in f)
        assert bits == 8 and chans == 0 and fmt == 0 and looplen == 0, name
        e = ents[name[1:].upper()]
        assert e.method == 0
        rows[ident] = (e.offset, e.size, rate if rate > 0 else 11025, vol & 0x7FFF, 1 if loopcnt != 0 else 0,
                       zlib.crc32(sound[e.offset:e.offset + e.size]) & 0xFFFFFFFF)
    print("samples %d" % len(rows))
    for i in range(SAMPLES):
        if i in rows:
            off, size, rate, vol, loop, crc = rows[i]
            print("sample %02x addr=%d len=%d rate=%d vol=%d loop=%d crc=%08x" % (i, off, size, rate, vol, loop, crc))
        else:
            print("sample %02x addr=0 len=0 rate=0 vol=0 loop=0 crc=00000000" % i)
    misc = datfile.extract(datfile.read(os.path.join(game, "DATA", "GAME.DAT")), "MISC.DTR").decode("latin-1").replace("\r", "")
    sec = misc.split("CD_TRACKS", 1)[1].split("END_LIST", 1)[0]
    cd = [num(t) for t in re.sub(r"(?m)^\s*\*.*$", "", sec).split()]
    assert len(cd) == 7
    print("cd_tracks 1 " + " ".join(str(c) for c in cd))


main()
