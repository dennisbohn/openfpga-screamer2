"""The inputs of tools/check_zip.sh (the native test of firmware/zip.c), written to a folder:

    python -I tools/check_zip.py <SCREAMER2.zip> <out folder>

  real.man     for every file of the real ZIP, what Python's zipfile says: name, size, CRC-32
  cases.txt    one test case a line: file, manifest, expected result of zip_open, expected file count
  *.zip, *.man the cases made here; a manifest line is
                 ok      name size crc   zip_find finds it and zip_read returns exactly these bytes
                 none    name            zip_find finds nothing
                 err     name codes      zip_read fails with one of these results (zip.h), comma separated
  defl1/6/9.zip   made-up bytes of every kind plus seven files of the real archive (game data, in
                  the build folder only), deflated by Python's zlib at level 1, 6 and 9
  fuzz.zip, fuzzdefl.zip   small archives the test damages at random

The small archives are written by hand the way Windows writes a stored ZIP: flag 0x8, the sizes and
the CRC in a data descriptor after the data and 0 in the local header, extra fields of changing
length so that the data starts at every offset modulo 4.
"""
import io
import os
import random
import struct
import sys
import zipfile
import zlib

EXTRAS = [b"", b"\xfe\xca\x00\x00", b"\xfe\xca\x01\x00X", b"\xfe\xca\x02\x00XY", b"UT\x05\x00\x01\x00\x00\x00\x00"]
REAL_FILES = ["DATA/GAME.DAT", "DATA/GEN.DAT", "DATA/LOCAL.DAT", "DATA/MAP1.DAT", "GFX/GAME1.DAT", "DATA/SOUND.DAT",
              "GFX/MAP1H01.DAT"]
# result codes of zip.h
ZIP_NOT_ZIP, ZIP_ZIP64, ZIP_TOO_MANY, ZIP_BAD, ZIP_METHOD, ZIP_RANGE, ZIP_CRC, ZIP_INFLATE = 3, 4, 5, 6, 7, 8, 9, 10


def make_zip(entries, comment=b""):
    """entries: (name, data); a name ending in '/' is a folder. Stored, Windows style."""
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


def zipfile_zip(path, entries, level=6):
    """entries: (name, data, method) written by Python's zipfile (deflated at level)"""
    with zipfile.ZipFile(path, "w") as zf:
        for name, data, method in entries:
            zf.writestr(name, data, compress_type=method, compresslevel=level)
    return open(path, "rb").read()


def blob(seed, n):
    return random.Random(seed).randbytes(n)


def text(seed, n):
    """words, so that the Huffman codes get skewed (a dynamic block)"""
    r = random.Random(seed)
    words = [r.randbytes(r.randrange(2, 9)).hex() for _ in range(60)]
    out = b""
    while len(out) < n:
        out += (r.choice(words) + " ").encode()
    return out[:n]


def man(lines):
    return "".join("\t".join(str(x) for x in l) + "\n" for l in lines)


def ok(name, data):
    return ("ok", name, len(data), "%08x" % zlib.crc32(data))


def central_field(zbytes, name, offset, fmt, value):
    """patch a field of the central directory record of name (offset in the record)"""
    at = zbytes.rindex(b"PK\x01\x02", 0, zbytes.rindex(b"PK\x05\x06"))
    while True:
        nlen = struct.unpack_from("<H", zbytes, at + 28)[0]
        if zbytes[at + 46:at + 46 + nlen] == name.encode():
            break
        at = zbytes.rindex(b"PK\x01\x02", 0, at)
    b = bytearray(zbytes)
    struct.pack_into(fmt, b, at + offset, value)
    return bytes(b)


def data_start(zbytes, name):
    """file offset of the first data byte of name"""
    zi = zipfile.ZipFile(io.BytesIO(zbytes)).getinfo(name)
    nlen, xlen = struct.unpack_from('<HH', zbytes, zi.header_offset + 26)
    return zi.header_offset + 30 + nlen + xlen


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

    # deflated: made-up bytes of every kind (empty, one byte, fixed and dynamic Huffman codes, long matches
    # that overlap themselves, incompressible bytes that become stored blocks, also across the 64 KB
    # chunks) and seven real files, at three levels; and stored and deflated entries in one archive
    made = [("DATA/S0.DAT", b""), ("DATA/S1.DAT", b"x"), ("DATA/S5.DAT", b"hello"), ("DATA/Z.DAT", bytes(100000)),
            ("DATA/P.DAT", bytes(range(256)) * 300), ("DATA/R300.DAT", blob(7, 300)), ("DATA/R70K.DAT", blob(8, 70001)),
            ("DATA/T20K.DAT", text(1, 20000)), ("DATA/T200K.DAT", text(2, 200000)),
            ("DATA/MIX.DAT", text(3, 5000) + blob(4, 3000) + bytes(9000) + text(5, 7000))]
    realdata = [(n, z.read(n)) for n in REAL_FILES]
    for level in (1, 6, 9):
        allent = made + realdata
        data = zipfile_zip(os.path.join(out, "_t.tmp"), [(n, d, zipfile.ZIP_DEFLATED) for n, d in allent], level)
        put("defl%d.zip" % level, data, [ok(n, d) for n, d in allent], 0, len(allent))
    os.remove(os.path.join(out, "_t.tmp"))
    mix = [(n, d, zipfile.ZIP_STORED if k % 2 else zipfile.ZIP_DEFLATED) for k, (n, d) in enumerate(made)]
    data = zipfile_zip(os.path.join(out, "_t.tmp"), mix)
    put("mixed.zip", data, [ok(n, d) for n, d, _ in mix], 0, len(mix))
    os.remove(os.path.join(out, "_t.tmp"))

    # a wrong CRC in the directory, damaged streams, a stream the directory says is shorter
    a, b = text(11, 20000), text(12, 30000)
    base = zipfile_zip(os.path.join(out, "_t.tmp"), [("DATA/A.DAT", a, 8), ("DATA/B.DAT", b, 8)], 9)
    put("badcrc.zip", central_field(base, "DATA/A.DAT", 16, "<I", zlib.crc32(a) ^ 0x1000),
        [("err", "DATA/A.DAT", ZIP_CRC), ok("DATA/B.DAT", b)], 0, 2)
    at = data_start(base, "DATA/A.DAT")
    csize = zipfile.ZipFile(io.BytesIO(base)).getinfo("DATA/A.DAT").compress_size
    damaged = []
    for k, pos in enumerate((2, csize // 3, csize - 6, csize // 2)):
        c = bytearray(base)
        if k == 3:
            c[at + pos:at + pos + 8] = bytes(8)
        else:
            c[at + pos] ^= 0x5A
        damaged.append(bytes(c))
    # one archive per damage (the four cases are told apart by the name)
    for k, c in enumerate(damaged):
        put("corrupt%d.zip" % k, c, [("err", "DATA/A.DAT", "%d,%d" % (ZIP_CRC, ZIP_INFLATE)), ok("DATA/B.DAT", b)], 0, 2)
    put("cut.zip", central_field(base, "DATA/A.DAT", 20, "<I", csize // 2),
        [("err", "DATA/A.DAT", "%d,%d" % (ZIP_CRC, ZIP_INFLATE)), ok("DATA/B.DAT", b)], 0, 2)
    os.remove(os.path.join(out, "_t.tmp"))

    # the methods the reader does not have
    other = zipfile_zip(os.path.join(out, "_t.tmp"), [("DATA/BZ.DAT", a, zipfile.ZIP_BZIP2), ("DATA/XZ.DAT", a, zipfile.ZIP_LZMA),
                                                      ("DATA/OK.DAT", b, zipfile.ZIP_STORED)])
    put("methods.zip", other, [("err", "DATA/BZ.DAT", ZIP_METHOD), ("err", "DATA/XZ.DAT", ZIP_METHOD), ok("DATA/OK.DAT", b)], 0, 3)
    os.remove(os.path.join(out, "_t.tmp"))

    # a small archive of deflated entries (a dynamic block, a fixed one, stored blocks) for the fuzz
    fz = zipfile_zip(os.path.join(out, "_t.tmp"), [("DATA/D.DAT", text(21, 6000), 8), ("DATA/E.DAT", b"abcabcabcabc" * 3, 8),
                                                   ("DATA/F.DAT", blob(22, 200), 8)], 9)
    put("fuzzdefl.zip", fz, [], 0, 3)
    os.remove(os.path.join(out, "_t.tmp"))

    # not a ZIP, cut off (the end record gone, also in the middle of the data), too many files, ZIP64,
    # a directory behind the end record
    put("notzip.zip", blob(5, 100000), [], ZIP_NOT_ZIP, 0)
    put("tiny.zip", b"PK\x05\x06", [], ZIP_NOT_ZIP, 0)
    put("truncated.zip", small[:-30], [], ZIP_NOT_ZIP, 0)
    put("truncated2.zip", small[:len(small) // 2], [], ZIP_NOT_ZIP, 0)
    put("many.zip", make_zip([("DATA/F%d.DAT" % k, b"x") for k in range(600)]), [], ZIP_TOO_MANY, 0)
    z64 = bytearray(small)
    z64[-22 + 10:-22 + 12] = b"\xff\xff"
    put("zip64.zip", bytes(z64), [], ZIP_ZIP64, 0)
    dmg = bytearray(small)
    dmg[-22 + 16:-22 + 20] = struct.pack("<I", len(small))
    put("damaged.zip", bytes(dmg), [], ZIP_BAD, 0)

    with open(os.path.join(out, "cases.txt"), "w", newline="\n") as f:
        for c in cases:
            f.write("\t".join(str(x) for x in c) + "\n")
    print("%d files in the real ZIP, %d cases in %s" % (len(files), len(cases), out))


main()
