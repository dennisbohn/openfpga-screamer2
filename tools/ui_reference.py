"""Independent references for the 2D toolkit (firmware/s2gfx.c) from the game's own files, for tools/menu_screens.sh.

Usage: python -I tools/ui_reference.py <game folder> <out dir>

Writes into <out dir> (never committed):
  expect.txt       "bkk <NAME> <CRC-32 of the 64,000 pixels> <CRC-32 of the palette as 256 little-endian words 0x00RRGGBB>" for every
                   320 x 200 .BKK of GRAF1.DAT, and "snd <n> <bytes> <rate>" for the 15 samples of SOUND.DAT's MENU.DTR
  cases.txt        the text and sprite cases the harness draws: "text <font> <colour> <space> <x> <y> <string>", "spr <n> <x> <y>",
                   "cen <n> <y>", "ctext <font> <colour> <space> <y> <string>"
  cases.idx        the same cases drawn by this script's own renderer into a 320 x 200 index frame (pixel = colour * 16 + shade)
  frame_<k>.idx/.pal   race pictures (docs/screamer2/*.png of the repository) quantized to a track's palette, for the pause screens
  graf_deflated.zip    GFX/GRAF1.DAT and DATA/LOCAL.DAT in a deflated ZIP, for the firmware's path for a compressed ZIP
The readers here (LZW, the S2 .SPR / .FNT layout, the text routine) are written from the file formats and the original's routines, not
from the firmware.
"""
import os
import struct
import sys
import zlib

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(root, "tools"))
import datfile  # noqa: E402


def container(game, rel):
    return datfile.read(os.path.join(game, rel))


def bkk(data):
    magic, colours, w, h, n, _ = struct.unpack_from("<6I", data, 0)
    assert (magic, colours, w, h, n) == (0x32353656, 256, 320, 200, 64000), "not a 320 x 200 .BKK"
    pal = [(data[24 + 3 * i] & 63) * 4 << 16 | (data[25 + 3 * i] & 63) * 4 << 8 | (data[26 + 3 * i] & 63) * 4 for i in range(256)]
    return pal, data[24 + 768:24 + 768 + 64000]


def sprites(spr):
    n, size = struct.unpack_from("<2I", spr, 0)
    out = []
    for i in range(n):
        x, y, w, h, at = struct.unpack_from("<5i", spr, 8 + 20 * i)
        base = 8 + 20 * n + at
        out.append((x, y, w, h, spr[base:base + w * h]))
    return out


def fonts(fnt):
    n = fnt[0]
    heads = [struct.unpack_from("<4i", fnt, 1 + 32 * k) for k in range(n)]
    p = 1 + 32 * n
    tables = []
    for h in heads:
        tables.append([struct.unpack_from("<2i", fnt, p + 8 * i) for i in range(h[2])])
        p += 8 * h[2]
    size = struct.unpack_from("<i", fnt, p)[0]
    px = p + 4
    assert px + size == len(fnt)
    out = []
    for h, t in zip(heads, tables):
        glyphs = []
        for w, off in t:
            bw = (w + 1) // 2
            rows = []
            for r in range(h[0]):
                row = fnt[px + off + r * bw:px + off + (r + 1) * bw]
                rows.append([(row[c >> 1] >> (0 if c & 1 else 4)) & 15 for c in range(w)])
            glyphs.append((w, rows))
        out.append({"height": h[0], "first": h[1], "count": h[2], "glyphs": glyphs, "cell": h[3] - 2})
    return out


class Canvas:
    def __init__(self):
        self.px = bytearray(320 * 200)

    def put(self, x, y, v):
        if 0 <= x < 320 and 0 <= y < 200:
            self.px[y * 320 + x] = v

    def sprite(self, spr, n, x, y):
        sx, sy, w, h, px = spr[n]
        for r in range(h):
            for c in range(w):
                if px[r * w + c]:
                    self.put(x + sx + c, y + sy + r, px[r * w + c])

    @staticmethod
    def layout(font, space, s):
        """The engine's text routine (STARTL.EXE 0xc67f0 draws, 0xc6c88 measures), one step at a time as the machine code does it: the pointer
        advances by the glyph's width, and by `space` more before every glyph but the first; after the control byte 1 a glyph sits in a cell of
        (the font's fourth number - 2) pixels, centred by an arithmetic shift of the cell less the glyph's width, and the cell grows by `space`
        once the first glyph is through. Returns [(char, glyph x offset)] and the width."""
        f, cell, fixed, started, x, out = font, font["cell"], False, False, 0, []
        for ch in s.encode("latin-1"):
            if not f["first"] <= ch < f["first"] + f["count"]:
                fixed = True if ch == 1 else False if ch == 2 else fixed
                continue
            w = f["glyphs"][ch - f["first"]][0]
            if fixed:
                out.append((ch, x + ((cell - w) >> 1)))
                x += cell
            else:
                at = x + space if started else x
                out.append((ch, at))
                x = at + w
            if not started:
                cell += space
            started = True
        return out, x

    def text_width(self, font, space, s):
        return self.layout(font, space, s)[1]

    def text(self, font, colour, space, x, y, s):
        for ch, gx in self.layout(font, space, s)[0]:
            w, rows = font["glyphs"][ch - font["first"]]
            for r, row in enumerate(rows):
                for c, shade in enumerate(row):
                    if shade:
                        self.put(x + gx + c, y + r, colour * 16 + shade)


def read_png_rgb(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", d[16:29])
    assert (bd, ct, il) == (8, 2, 0)
    idat, p = b"", 8
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    raw = zlib.decompress(idat)
    stride, prev, rows = 3 * w, bytearray(3 * w), []
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    graf = container(game, "GFX/GRAF1.DAT")
    lines = []
    for e in datfile.entries(graf):
        if e.name.upper().endswith("L.BKK"):
            pal, px = bkk(datfile.extract(graf, e.name))
            lines.append(f"bkk {e.name} {zlib.crc32(px):08x} {zlib.crc32(struct.pack('<256I', *pal)):08x}")
    sound = container(game, "DATA/SOUND.DAT")
    dtr = datfile.extract(sound, "MENU.DTR").decode("latin-1").split()
    names = {e.name.upper(): e for e in datfile.entries(sound)}
    for i, w in enumerate(dtr):
        if w.startswith("s") and w[1:].upper().endswith(".RAW"):
            n = int(dtr[i + 1][1:], 16)
            rate = int(dtr[i + 5][1:])
            lines.append(f"snd {n} {names[w[1:].upper()].size} {rate}")
    open(os.path.join(out, "expect.txt"), "w").write("\n".join(lines) + "\n")

    spr = sprites(datfile.extract(graf, "MENUL.SPR"))
    fnt = fonts(datfile.extract(graf, "MENUL.FNT"))
    cases = [
        "text 1 1 2 10 10 RESUME", "text 1 1 2 10 40 RESTART", "text 1 15 2 120 10 EXIT", "text 0 1 2 10 70 SCREAMER 2",
        "text 2 1 2 10 110 BEST TIME  ?@[]{}~", "text 4 2 3 160 110 Lowercase and 0123456789", "text 3 0 2 10 130 --:--:-- MIL",
        "text 5 1 2 200 150 0123456789", "text 1 1 2 -5 180 clipped at the left", "text 1 1 2 250 160 clipped at the right",
        "text 1 1 2 10 150 \x01" + "00:12:34", "spr 19 10 10", "spr 35 200 20", "spr 36 100 100", "spr 0 220 100", "cen 19 4", "cen 35 60",
        "ctext 1 1 2 170 LOADING...", "ctext 2 1 2 72 BEST TIME", "ctext 0 2 2 40 CHAMPIONSHIP",
    ]
    c = Canvas()
    for s in cases:
        w = s.split(" ", 1)
        kind = w[0]
        if kind == "text":
            f, col, sp, x, y, txt = s.split(" ", 6)[1:]
            c.text(fnt[int(f)], int(col), int(sp), int(x), int(y), txt)
        elif kind == "ctext":
            f, col, sp, y, txt = s.split(" ", 5)[1:]
            tw = c.text_width(fnt[int(f)], int(sp), txt)
            c.text(fnt[int(f)], int(col), int(sp), (320 - tw) >> 1, int(y), txt)
        elif kind == "spr":
            n, x, y = [int(v) for v in s.split()[1:]]
            c.sprite(spr, n, x, y)
        elif kind == "cen":
            n, y = [int(v) for v in s.split()[1:]]
            sx, _, w_, _, _ = spr[n]
            c.sprite(spr, n, (320 - (w_ + sx)) // 2 - (sx - (1 if sx < 0 else 0)) // 2, y)
    open(os.path.join(out, "cases.txt"), "w", newline="\n").write("\n".join(cases) + "\n")
    open(os.path.join(out, "cases.idx"), "wb").write(bytes(c.px))

    # race pictures (the repository's renders) quantized to the palette of the track they show
    for name, track in (("s2_track4_city", 4), ("s2_track1_village", 1), ("s2_track5_snow", 5)):
        gfx = container(game, f"GFX/MAP{track}H01.DAT")
        col = datfile.extract(gfx, f"MAP{track}.COL")
        pal = [(col[3 * i] * 4, col[3 * i + 1] * 4, col[3 * i + 2] * 4) for i in range(256)]
        w, h, rows = read_png_rgb(os.path.join(root, "docs", "screamer2", name + ".png"))
        first = {}
        for i, p in enumerate(pal):
            first.setdefault(p, i)
        px = bytearray()
        for y in range(h):
            for x in range(w):
                rgb = tuple(rows[y][3 * x:3 * x + 3])
                i = first.get(rgb)
                if i is None:
                    i = min(range(256), key=lambda k: sum((a - b) ** 2 for a, b in zip(pal[k], rgb)))
                px.append(i)
        open(os.path.join(out, f"frame_{name}.idx"), "wb").write(bytes(px))
        open(os.path.join(out, f"frame_{name}.pal"), "wb").write(b"".join(struct.pack("<I", p[0] << 16 | p[1] << 8 | p[2]) for p in pal))
    import zipfile
    with zipfile.ZipFile(os.path.join(out, "graf_deflated.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(game, "GFX", "GRAF1.DAT"), "GFX/GRAF1.DAT")
        z.write(os.path.join(game, "DATA", "LOCAL.DAT"), "DATA/LOCAL.DAT")
    print(f"{len(lines)} expectations, {len(cases)} cases, 3 frames -> {out}")


main()
