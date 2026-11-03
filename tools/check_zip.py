"""The inputs of tools/check_zip.sh (the native test of firmware/zip.c), written to a folder:

    python -I tools/check_zip.py <SCREAMER2.zip> <out folder>

  real.man     for every file of the real ZIP, what Python's zipfile says: name, size, CRC-32
  cases.txt    one test case a line: file, manifest, expected result of zip_open, expected file count
  *.zip, *.man the negative cases made here from made-up bytes (no game data); a manifest line is
                 ok      name size crc   zip_find finds it and zip_read returns exactly these bytes
                 none    name            zip_find finds nothing
                 packed  name            found, but zip_read says it is compressed
  fuzz.zip     the small archive the test damages at random

The small archives are written by hand the way Windows writes a stored ZIP: flag 0x8, the sizes and
the CRC in a data descriptor after the data and 0 in the local header, extra fields of changing
length so that the data starts at every offset modulo 4.
"""
import os
import random
import struct
import sys
import zipfile
import zlib

EXTRAS = [b"", b"\xfe\xca\x00\x00", b"\xfe\xca\x01\x00X", b"\xfe\xca\x02\x00XY", b"UT\x05\x00\x01\x00\x00\x00\x00"]


def make_zip(entries, comment=b""):
    """entries: (name, data); a name ending in '/' is a folder."""
    out, cd = b"", b""
    for k, e in enumerate(entries):
        name, data = e[0].encode(), e[1]
        extra = EXTRAS[k % len(EXTRAS)]
        crc = zlib.crc32(data)
        folder = name.endswith(b"/")
        flag = 0 if folder else 8
        pos = len(out)
        out += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, flag, 0, 0, 0x21, 0, 0, 0, len(name), len(extra))
        out += name + extra + data
        if not folder:
            out += struct.pack("<IIII", 0x08074B50, crc, len(data), len(data))
        cd += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 63, 20, flag, 0, 0, 0x21, crc, len(data), len(data),
                          len(name), len(extra), 0, 0, 0, 0x10 if folder else 0, pos) + name + extra
    eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(entries), len(entries), len(cd), len(out), len(comment))
    return out + cd + eocd + comment


def blob(seed, n):
    return random.Random(seed).randbytes(n)


def man(lines):
    return "".join("\t".join(str(x) for x in l) + "\n" for l in lines)


def ok(name, data):
    return ("ok", name, len(data), "%08x" % zlib.crc32(data))


def main():
    real, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cases = []

    def put(fn, data, manifest, result, n):
        open(os.path.join(out, fn), "wb").write(data)
        open(os.path.join(out, fn[:-4] + ".man"), "w", newline="\n").write(man(manifest))
        cases.append((fn, fn[:-4] + ".man", result, n))

    # the real archive: every file the way zipfile reads it
    z = zipfile.ZipFile(real)
    files = [i for i in z.infolist() if not i.filename.endswith("/")]
    assert all(i.compress_type == 0 for i in files), "the real ZIP must be stored"
    assert z.testzip() is None, "a CRC-32 of the real ZIP is wrong"
    with open(os.path.join(out, "real.man"), "w", newline="\n") as f:
        for i in files:
            f.write("ok\t%s\t%d\t%08x\n" % (i.filename, i.file_size, i.CRC))
    cases.append(("REAL", "real.man", 0, len(files)))

    # sizes 0..20 and some more, so that the data ends at every offset modulo 4 too
    sizes = list(range(0, 21)) + [100, 4093, 4096, 70001]
    ent = [("DATA/", b"")] + [("DATA/F%d.DAT" % s, blob(s, s)) for s in sizes]
    small = make_zip(ent)
    mm = [ok(e[0], e[1]) for e in ent if not e[0].endswith("/")]
    put("small.zip", small, mm, 0, len(sizes))
    put("fuzz.zip", small, [], 0, len(sizes))

    # wrong top folder, a folder prefix, other case: matched on the end of the path
    d1, d2, d3 = blob(1, 1000), blob(2, 777), blob(3, 5)
    put("wrongtop.zip", make_zip([
        ("Screamer 2/", b""), ("Screamer 2/DATA/", b""), ("Screamer 2/DATA/GAME.DAT", d1),
        ("./GFX/map1h01.dat", d2), ("Old\\GFX\\GAME1.DAT", d3), ("XDATA/GEN.DAT", d3), ("DATA/GEN.DAT.bak", d3),
        ("Screamer 2/CONFIG/CHOICE.DAT", d3)]),
        [ok("DATA/GAME.DAT", d1), ok("GFX/MAP1H01.DAT", d2), ok("GFX/GAME1.DAT", d3), ok("data/game.dat", d1),
         ("none", "DATA/GEN.DAT"), ("none", "GFX/GRAF1.DAT"), ("none", "DATA/GAME.DAT.bak")], 0, 6)

    # a comment behind the end record (with a fake record in it), and an archive with nothing in it
    put("comment.zip", make_zip(ent, b"PK\x05\x06 not an end record " + blob(9, 40)), mm, 0, len(sizes))
    put("empty.zip", make_zip([]), [("none", "DATA/GAME.DAT")], 0, 0)

    # compressed (what Compress-Archive and the Explorer of Windows 10 make): listed, not readable
    big = bytes(range(256)) * 40
    tmp = os.path.join(out, "_deflate.tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("DATA/GAME.DAT", big)
        zf.writestr("GFX/GAME1.DAT", big)
    put("deflate.zip", open(tmp, "rb").read(), [("packed", "DATA/GAME.DAT"), ("packed", "GFX/GAME1.DAT")], 0, 2)
    os.remove(tmp)

    # not a ZIP, cut off (the end record gone, also in the middle of the data), too many files, ZIP64,
    # a directory behind the end record
    put("notzip.zip", blob(5, 100000), [], 3, 0)
    put("tiny.zip", b"PK\x05\x06", [], 3, 0)
    put("truncated.zip", small[:-30], [], 3, 0)
    put("truncated2.zip", small[:len(small) // 2], [], 3, 0)
    put("many.zip", make_zip([("DATA/F%d.DAT" % k, b"x") for k in range(600)]), [], 5, 0)
    z64 = bytearray(small)
    z64[-22 + 10:-22 + 12] = b"\xff\xff"
    put("zip64.zip", bytes(z64), [], 4, 0)
    dmg = bytearray(small)
    dmg[-22 + 16:-22 + 20] = struct.pack("<I", len(small))
    put("damaged.zip", bytes(dmg), [], 6, 0)

    with open(os.path.join(out, "cases.txt"), "w", newline="\n") as f:
        for c in cases:
            f.write("\t".join(str(x) for x in c) + "\n")
    print("%d files in the real ZIP, %d cases in %s" % (len(files), len(cases), out))


main()
