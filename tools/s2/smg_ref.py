"""The see-through table of Screamer 2 and its compact form (firmware/smg.c is checked against this reading of the rules).

What the original does (STARTL.EXE, docs/screamer2/comparison.md "See-through table"): the filler of routine 208 / 224 (0xc75cc / 0xc74e0) turns each pixel into
SMG[texel * 256 + pixel], and the program makes that table itself at the start of a race, 112 rows of 256 palette indices (0x506c2 and 0x50a8e of the image
tools/dis.sh -f reads; the notes' 0x504f2 and 0x508be), from the track's palette (6-bit values) and TRACK_DATA's asmoke and a2smoke:
    rows   0 .. 31   the palette moved towards asmoke / 4, row / 31 of the way        (ramp "smoke")
    rows  32 .. 47   towards black, row / 15                                           ("shadow")
    rows  48 .. 79   towards a2smoke / 4, row / 31                                      ("smoke2")
    rows  80 .. 95   brighter, x (1 + row / 15), no component over 63                   ("bright")
    rows  96 .. 111  towards white (63, 63, 63), row / 15                               ("white")
each entry the palette's nearest colour to what the colour became (squared distance of the three components, the first of equals); with all three of asmoke
negative (the menu's rooms) only the shadow's 16 rows are made, and the last two ramps are the race's alone (0x50a8e is not called by the menu).

The hardware has 64 rows and a 6-bit texel index (core/src/fpga/core/raster_tex.v), and texel 0 leaves the pixel alone: so the firmware makes a compact table
at the start (tools/s2/make_track_s2.py's image carries it): the see-through textures of the image's libraries are scanned, each distinct texel 1 .. 111 an
effect uses gets a row of its own (rows 1 .. 62; row 63 is the radar's), the textures are copied with their texels turned into those rows, their materials
turned into routine 224 (the hardware's), and the table has, for each row, the original's row of that texel. More than 62 distinct texels: the closest pair
of one ramp merges (the lower into the higher) until they fit. A texel of 112 or more (the original reads past its table there: never meant) and a texel
of a ramp the race does not make are texel 0.

The nearest colour is worked out in whole numbers (the target scaled by the ramp's rows - 1): the original's x87 sums differ from that only where two colours
are exactly as near (they break such a tie by rounding noise: 0.1 % of the entries, docs/development.md "Wheel smoke and shadows").
"""
import struct

SMOKE, SHADOW, SMOKE2, BRIGHT, WHITE = 1, 2, 4, 8, 16
FIRST_TEXEL = (0, 32, 48, 80, 96)       # the ramps' first rows in the original
RAMP = (SMOKE, SHADOW, SMOKE2, BRIGHT, WHITE)
ROWS = (32, 16, 32, 16, 16)
FREE = 62                               # compact rows 1 .. 62


def ramp_of(t):
    """the ramp (index 0 .. 4) of the original's texel t, None for t outside 1 .. 111"""
    if not 1 <= t <= 111:
        return None
    return max(k for k in range(5) if t >= FIRST_TEXEL[k])


def original_row(pal6, t, asmoke, a2smoke):
    """the original's row t: 256 palette indices. pal6: 256 (r, g, b) of 6-bit values; asmoke, a2smoke: TRACK_DATA's rgb (shifted right by 2 as the program does)"""
    k = ramp_of(t)
    l = t - FIRST_TEXEL[k]
    n = ROWS[k] - 1
    p = [tuple(v & 63 for v in c) for c in pal6]
    near = {}                                                           # per distinct target: the first palette entry nearest to it
    out = bytearray(256)
    for c in range(256):
        if k == 3:                                                       # the colour x (1 + l / 15), no component over 63, in 15ths
            tgt = tuple(min(v * (15 + l), 63 * 15) for v in p[c])
        else:
            to = {0: [v >> 2 for v in asmoke], 1: (0, 0, 0), 2: [v >> 2 for v in a2smoke], 4: (63, 63, 63)}[k]
            tgt = tuple(v * n + (w - v) * l for v, w in zip(p[c], to))
        if tgt not in near:
            near[tgt] = min(range(256), key=lambda i: (sum((v * n - w) ** 2 for v, w in zip(p[i], tgt)), i))     # (the lower index of equals)
        out[c] = near[tgt]
    return bytes(out)


def available(asmoke, race):
    """the ramps a race (or a room) makes"""
    if race and any(v >= 0 for v in asmoke):
        return SMOKE | SHADOW | SMOKE2 | BRIGHT | WHITE
    return SHADOW


def used_texels(texels, ramps):
    """the texels of `texels` (a set of byte values) that have a row in the table the original makes"""
    return sorted(t for t in texels if ramp_of(t) is not None and RAMP[ramp_of(t)] & ramps)


def plan(texels, ramps):
    """texel -> compact row (1 .. 62) for every texel the table has; the closest pair of a ramp merges (the lower into the higher) while there are more than 62"""
    alive = used_texels(texels, ramps)
    into = {t: t for t in alive}
    while len(alive) > FREE:
        gap, at = None, None
        for a, b in zip(alive, alive[1:]):
            if ramp_of(a) == ramp_of(b) and (gap is None or b - a < gap):
                gap, at = b - a, a
        assert at is not None, "more than 62 texels, none of them in one ramp"
        i = alive.index(at)
        into[at] = alive[i + 1]
        del alive[i]
    row = {t: 1 + alive.index(t) for t in alive}
    def end(t):
        while into[t] != t:
            t = into[t]
        return t
    return {t: row[end(t)] for t in into}, alive


def table(pal6, asmoke, a2smoke, race, alive):
    """the 64 x 256 table: row r (1 ..) is the original's row of alive[r - 1]"""
    out = bytearray(64 * 256)
    for r, t in enumerate(alive, 1):
        out[256 * r:256 * r + 256] = original_row(pal6, t, asmoke, a2smoke)
    return bytes(out)


i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]


def materials(recs, nrec, mlb):
    """(index, routine, texture record offset, w, h, frame offsets) of the see-through materials of a record table whose texture records and pixels are in mlb;
    one whose record or frames are not wholly in the file is left out"""
    out = []
    for k in range(nrec):
        routine, ro, w, h = (i32(recs, 16 * k + o) for o in (0, 4, 8, 12))
        if routine not in (208, 224) or ro <= 0 or ro + 12 > len(mlb) or not (1 <= w <= 256 and 1 <= h <= 256):
            continue
        cnt = i32(mlb, ro)
        n = cnt & 0xFFFF if cnt > 0 else 1
        if not 1 <= n <= 64 or ro + 8 + 4 * n > len(mlb):
            continue
        offs = [i32(mlb, ro + 8 + 4 * f) for f in range(n)]
        if any(o < 0 or o + 256 * (h - 1) + w > len(mlb) for o in offs):
            continue
        out.append((k, routine, ro, w, h, offs))
    return out


def copies(front, nfront, mlb, ntrack, ramps):
    """The see-through textures of the libraries copied. front: the car (and MISC) libraries' material records (nfront of 16 bytes, in front of the MLB), mlb: the MLB
    section so far (the track's MLB first, its ntrack records, then the libraries and the windows' picture; a multiple of 256 bytes), ramps: smg_ref.available().
    Returns (front, mlb, texel -> row, the texels the rows stand for, the distinct texels found): every material of routine 208 or 224 whose texture lies wholly in
    mlb is routine 224 on a copy of its texture (the record, then the frames' bytes from the first frame's to the last's, rows as texels) appended to mlb, the
    texels turned into rows; materials with the same texture record share the copy"""
    front, mlb = bytearray(front), bytearray(mlb)
    sets = [(front, nfront), (mlb, ntrack)]
    mats = [[(recs, m) for m in materials(recs, n, mlb)] for recs, n in sets]
    mats = [x for g in mats for x in g]
    found = set()
    for _, (k, routine, ro, w, h, offs) in mats:
        for o in offs:
            for y in range(h):
                found.update(mlb[o + 256 * y:o + 256 * y + w])
    found.discard(0)
    rowmap, alive = plan(found, ramps)
    lut = bytes(rowmap.get(t, 0) for t in range(256))
    copied = {}                                                     # texture record -> its copy's offset
    pad = lambda n: (n + 255) & ~255
    for recs, (k, routine, ro, w, h, offs) in mats:
        if ro not in copied:
            lo, hi = min(offs), max(offs) + 256 * (h - 1) + w
            cnt = i32(mlb, ro)
            head = pad(8 + 4 * len(offs))
            at = len(mlb)
            rec = struct.pack("<2i", cnt, i32(mlb, ro + 4)) + b"".join(struct.pack("<I", at + head + o - lo) for o in offs)
            mlb += rec + bytes(head - len(rec)) + mlb[lo:hi].translate(lut) + bytes(pad(hi - lo) - (hi - lo))
            copied[ro] = at
        struct.pack_into("<2i", recs, 16 * k, 224, copied[ro])
    return bytes(front), mlb, rowmap, alive, found


def process(front, nfront, mlb, ntrack, pal6, asmoke, a2smoke, race, meshes=()):
    """The see-through step of an image: copies() and then the effects' block at the end of the MLB section: the meshes (each padded to 256 bytes), the 64 x 256
    table and the directory of 256 bytes, the section's last:
        'S2FX', 1, the table's offset, the three meshes' offsets (0 for none), the rows used, the texels found, zeros
    Returns (front, mlb, texel -> row, the texels of the rows)"""
    front, mlb, rowmap, alive, found = copies(front, nfront, mlb, ntrack, available(asmoke, race))
    offs = []
    for m in meshes:
        offs.append(len(mlb))
        mlb += m + bytes(-len(m) % 256)
    offs += [0] * (3 - len(offs))
    t_at = len(mlb)
    mlb += table(pal6, asmoke, a2smoke, race, alive) + struct.pack("<4s7I", b"S2FX", 1, t_at, *offs, len(alive), len(found)).ljust(256, b"\0")
    return front, bytes(mlb), rowmap, alive
