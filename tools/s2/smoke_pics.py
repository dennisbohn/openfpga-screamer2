"""The firmware's smoke and shadows against the original's screenshots (docs/development.md, "Wheel smoke and shadows").

Usage: python -I tools/s2/smoke_pics.py <screenshot.png> <frames folder> <first frame> <last frame> [out prefix]

The screenshot is one of tools/s2/dosbox_smoke_s2.py's (raw 320 x 200, the game's own palette in the file); the folder holds nofx_<n>.rgb, shadow_<n>.rgb, puff_<n>.rgb: the
firmware's pictures of the recorded frames n without the effects, with only the shadows and with only the puffs (tools/s2/check_fx_s2.sh render; NOFX=1, FXONLY=shadow / puff), from
the recorded camera and cars. Which recorded frame the screenshot is of is not known to the frame (DOSBox takes the next one after the key): the frame whose picture is nearest to the
screenshot in the part of the scene round the cars (the race screen, which the firmware's picture here does not have, lies outside it) is taken. The textures, the rain and the lights
are not the original's pixel for pixel (a quarter of the pixels of the scene round the cars agree), so the effects are compared by what they do to an area: in the pixels the
firmware's shadows (puffs) change, the mean brightness (the mean of the palette's red, green and blue, 6-bit values) of the original's screenshot, of the firmware's picture without
the effect and with it. The effect is the same if the original's brightness over the firmware's picture without the effect is about what the firmware's picture with it gives.
out prefix: writes <prefix>_orig.ppm, _nofx.ppm, _shadow.ppm, _puff.ppm for a look.
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    pos, plte, idat = 8, None, b""
    while pos < len(d):
        n, = struct.unpack_from(">I", d, pos)
        kind, body = d[pos + 4:pos + 8], d[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", body[:10])
        elif kind == b"PLTE":
            plte = body
        elif kind == b"IDAT":
            idat += body
    assert (w, h, bd, ct) == (320, 200, 8, 3), "not a raw 320 x 200 screenshot"
    raw, rows, prev = zlib.decompress(idat), [], bytearray(320)
    for y in range(h):
        f, line = raw[y * 321], bytearray(raw[y * 321 + 1:y * 321 + 321])
        for x in range(320):
            a, b, c = line[x - 1] if x else 0, prev[x], prev[x - 1] if x else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    pal = [tuple(v >> 2 for v in plte[3 * i:3 * i + 3]) for i in range(256)]
    return [tuple(pal[i] for i in r) for r in rows]


def read_rgb(path):
    d = open(path, "rb").read()
    return [[tuple(v >> 2 for v in d[3 * (y * 320 + x):3 * (y * 320 + x) + 3]) for x in range(320)] for y in range(200)]


def region(y, x):
    """the part of the scene round the cars: not the race screen's corners, bars and dial"""
    return 40 <= y < 190 and 40 <= x < 280 and not (x >= 225 and y >= 118)


def write_ppm(path, pic):
    with open(path, "wb") as f:
        f.write(b"P6 320 200 255\n" + b"".join(bytes(min(255, v * 4) for v in px) for row in pic for px in row))


def main():
    shot, folder, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    out = sys.argv[5] if len(sys.argv) > 5 else None
    orig = read_png(shot)
    best = None
    for n in range(first, last + 1):
        nofx = read_rgb(f"{folder}/nofx_{n}.rgb")
        diff = sum(1 for y in range(200) for x in range(320) if region(y, x) and nofx[y][x] != orig[y][x])
        if best is None or diff < best[0]:
            best = (diff, n)
    n = best[1]
    pics = {k: read_rgb(f"{folder}/{k}_{n}.rgb") for k in ("nofx", "shadow", "puff")}
    lum = lambda px: (px[0] + px[1] + px[2]) / 3.0
    print(f"{shot}: frame {n}")
    for name, key in (("shadow", "shadow"), ("puffs", "puff")):
        pix = [(y, x) for y in range(200) for x in range(320) if region(y, x) and pics[key][y][x] != pics["nofx"][y][x]]
        if not pix:
            print(f"  {name}: none in view")
            continue
        m = lambda pic: sum(lum(pic[y][x]) for y, x in pix) / len(pix)
        o, b, f = m(orig), m(pics["nofx"]), m(pics[key])
        print(f"  {name}: {len(pix)} pixels; mean brightness: original {o:.1f}, firmware without {b:.1f}, with {f:.1f}; "
              f"original / without {o / b:.2f}, with / without {f / b:.2f}")
    if out:
        write_ppm(out + "_orig.ppm", orig)
        for k, v in pics.items():
            write_ppm(f"{out}_{k}.ppm", v)


if __name__ == "__main__":
    main()
