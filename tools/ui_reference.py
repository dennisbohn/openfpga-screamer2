"""Independent references for the 2D toolkit (firmware/s2gfx.c) from the game's own files, for tools/menu_screens.sh.

Usage: python -I tools/ui_reference.py <game folder> <out dir>

Writes into <out dir> (never committed):
  expect.txt       "bkk <NAME> <CRC-32 of the 64,000 pixels> <CRC-32 of the palette as 256 little-endian words 0x00RRGGBB>" for every
                   320 x 200 .BKK of GRAF1.DAT, and "snd <n> <bytes> <rate>" for the 15 samples of SOUND.DAT's MENU.DTR
  cases.txt        the text and sprite cases the harness draws: "text <font> <colour> <space> <x> <y> <string>", "spr <n> <x> <y>",
                   "cen <n> <y>", "ctext <font> <colour> <space> <y> <string>"
  cases.idx        the same cases drawn by this script's own renderer into a 320 x 200 index frame (pixel = colour * 16 + shade)
  frame_<k>.idx/.pal   race pictures (docs/screamer2/*.png of the repository) quantized to a track's palette, for the pause screens
  graf_deflated.zip    GFX/GRAF1.DAT, GFX/GAME1.DAT and DATA/LOCAL.DAT in a deflated ZIP, for the firmware's path for a compressed ZIP
  rows.txt             the best times the page tests put into the save: "add <kind> <track> <list> <time> <name> <team> <car>", "replay ..."
  settings_<k>.idx, hs_<k>.idx, end_<k>.idx   the GAME SETTINGS, HI SCORES and end-of-race screens in the states the tests drive them to
                       (320 x 200 palette indices); .dk the passes of darkening each pixel is to get (0 .. 3: the tests apply the firmware's
                       own table, which they check on its own against a search in double precision); end_<k>.pal the race's palette
  arcade.txt, arc_*    the arcade pages' scripted runs and the frames they must draw (tools/ui_arcade_ref.py)
  league.txt, lg_*, leagueend.txt, lend_*   the championship's pages and end screens (tools/ui_league_ref.py)
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
    def __init__(self, px=None):
        self.px = bytearray(px) if px is not None else bytearray(320 * 200)
        self.clip_text = (0, 200)       # the rows text and sprites are cut to (the original's 0x51189 and 0x50f3c)
        self.clip_spr = (0, 200)

    def put(self, x, y, v, clip=(0, 200)):
        if 0 <= x < 320 and max(0, clip[0]) <= y < min(200, clip[1]):
            self.px[y * 320 + x] = v

    def sprite(self, spr, n, x, y):
        sx, sy, w, h, px = spr[n]
        for r in range(h):
            for c in range(w):
                if px[r * w + c]:
                    self.put(x + sx + c, y + sy + r, px[r * w + c], self.clip_spr)

    def text_boxed(self, font, colour, space, x0, width, y, s):
        w = self.text_width(font, space, s)     # (the engine's measure, 0xc6c88: no gap after the last character)
        self.text(font, colour, space, x0 + ((width - w) >> 1), y, s)

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
                        self.put(x + gx + c, y + r, colour * 16 + shade, self.clip_text)


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


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def time_text(t):
    """0x37e1e: "MM:SS:CC", the whole seconds and the hundredths of the rest cut off; an empty row is --:--:--"""
    t = f32(t)
    if not t > 0 or t > 3599.9:
        return "--:--:--"
    whole = int(t)
    return "%02d:%02d:%02d" % (whole // 60 % 100, whole % 60, int((t - whole) * 100.0))


class Lists:
    """The save's best-times lists as the original keeps them: a time goes ahead of the first row it is not slower than, the rows
    below move down (all their fields), the fifteenth falls out; an empty row is 3599.999, MIL, team (row & 3) + 1, car 1"""
    def __init__(self):
        self.l = {}

    def rows(self, kind, track, lst):
        return self.l.setdefault((kind, track, lst), [(f32(3599.999), "MIL", (r & 3) + 1, 1, 0) for r in range(15)])

    def add(self, kind, track, lst, time, name, team, car, replay=0):
        rows = self.rows(kind, track, lst)
        time = f32(time)
        at = next((i for i, r in enumerate(rows) if not time > r[0]), 15)
        if at < 15:
            rows.insert(at, (time, name, team, car, replay))
            del rows[15]
        return at if at < 15 else -1


# The best times the page tests put into the save (kind: 0 league, 1 arcade, 2 time attack; list 0 .. 3 the totals of 1 / 3 / 5 / 10
# laps, 4 the best laps): tools/host/ui_screen.c reads rows.txt, so both sides have the same lists
ROWS = [
    (1, 1, 1, 80.5, "AAA", 1, 1), (1, 1, 1, 82.25, "BBB", 2, 2), (1, 1, 1, 83.01, "CCC", 3, 3), (1, 1, 1, 83.01, "DDD", 4, 5),
    (1, 1, 1, 90.0, "EEE", 1, 4), (1, 1, 1, 95.5, "FFF", 2, 1), (1, 1, 1, 99.99, "GGG", 3, 2),
    (1, 1, 4, 27.4, "LAP", 1, 1), (1, 1, 4, 28.125, "LAQ", 2, 3), (1, 1, 4, 31.0, "LAR", 4, 4),
    (0, 3, 2, 200.5, "LEG", 2, 2), (2, 1, 1, 77.7, "ATK", 3, 4),
    (1, 4, 1, 70.0, "ONE", 1, 1), (1, 4, 1, 73.0, "TWO", 2, 2), (1, 4, 1, 76.0, "TRE", 3, 3), (1, 4, 1, 79.0, "FOR", 4, 4),
    (1, 4, 1, 82.0, "FIV", 1, 5), (1, 4, 1, 85.0, "SIX", 2, 1), (1, 4, 1, 88.0, "SEV", 3, 2), (1, 4, 1, 91.0, "EIG", 4, 3),
    (1, 4, 1, 94.0, "NIN", 1, 4),
] + [(1, 7, 0, 50.0 + i, "Q%02d" % i, i % 4 + 1, i % 5 + 1) for i in range(15)]
REPLAYS = {(1, 1, 4, 0): 5}                     # the row (kind, track, list, row) that has a replay number, once the rows are in

# The end screen's runs (tools/host/ui_screen.c has the same table): the race picture, the race, the player's results, the frames j
# (one per video frame from the finish, j = 0 the first) that are kept, the frame the d-pad's right is pressed in
END_RUNS = [
    ("A", "s2_track4_city", dict(track=4, laps=3, place=1, time=85.5, best=29.5, team=2, car=3), [100, 200, 231, 302, 400], 300),
    ("B", "s2_track1_village", dict(track=1, laps=3, place=3, time=140.125, best=45.0, team=4, car=2), [100, 400], None),
    ("C", "s2_track5_snow", dict(track=7, laps=1, place=4, time=99.0, best=0.0, team=1, car=1), [400], None),
    # time attack's end (0x48b4c, menus row 7): TOTAL, the time, LAP TIME and the best lap for four seconds, then the list of kind 2 (the three-lap totals)
    ("D", "s2_track1_village", dict(track=1, laps=3, place=1, time=78.25, best=26.5, team=1, car=1, attack=1), [100, 400], None),
]


def pages(game, out, graf, game1, spr, fnt):
    MEDIUM, SMALL, SMALL2, TITLE = 1, 2, 3, 0
    local = container(game, "DATA/LOCAL.DAT")
    strings = datfile.extract(local, "STRINGS.TXT").decode("latin-1").replace("\r", "").split("\n")
    lines = [s for s in strings if s and not s.startswith("*")]
    gtxt = [s for s in datfile.extract(local, "GAME.TXT").decode("latin-1").replace("\r", "").split("\n") if s and not s.startswith("*")]

    def at(anchor, k):
        return lines[lines.index(anchor) + k]

    with open(os.path.join(out, "rows.txt"), "w", newline="\n") as f:
        for r in ROWS:
            f.write("add %d %d %d %r %s %d %d\n" % (r[0], r[1], r[2], r[3], r[4], r[5], r[6]))
        for k, v in REPLAYS.items():
            f.write("replay %d %d %d %d %d\n" % (k + (v,)))

    def write(name, c, dk=None, ov=None, pal=None):
        open(os.path.join(out, name + ".idx"), "wb").write(bytes(c.px))
        if dk is not None:
            open(os.path.join(out, name + ".dk"), "wb").write(bytes(dk))
        if ov is not None:
            open(os.path.join(out, name + ".ov"), "wb").write(bytes(ov))

    lists = Lists()
    for r in ROWS:
        lists.add(r[0], r[1], r[2], r[3], r[4], r[5], r[6])
    for (kind, track, lst, row), n in REPLAYS.items():
        lists.rows(kind, track, lst)[row] = lists.rows(kind, track, lst)[row][:4] + (n,)

    # ---- GAME SETTINGS (BKG_7L): the states the test script reaches (cursor row, whether its value is lit, the eight values)
    pal7, px7 = bkk(datfile.extract(graf, "BKG_7L.BKK"))
    labels = ["TRANSMISSION", "DETAIL LEVEL", "NUMBER OF LAPS", "ARCADE ENEMIES SKILL", "MULTIPLAYER ENEMIES", "TIME ATTACK GHOST CAR", "DAMAGES",
              "MUSIC", "VIEW HIGH SCORES"]
    first = [0, 2, 5, 9, 12, 14, 16, 18]
    values = lines[lines.index(":AUTO"):lines.index(":AUTO") + 20]

    def settings(cursor, lit, vals):
        c = Canvas(px7)
        c.text_boxed(fnt[TITLE], 1, 2, 0, 320, 4, "GAME SETTINGS")
        for i in range(9):
            y = 0x30 + 14 * i
            hot = lit and cursor == i
            if i == 8:
                c.text(fnt[SMALL2 if hot else SMALL], 3 if hot else 1, 2, 10, y, labels[i])
                continue
            c.text(fnt[SMALL], 1, 2, 10, y, labels[i])
            v = values[first[i] + vals[i]]
            if hot:
                c.text(fnt[SMALL2], 3, 2, 200, y, v)
                c.text(fnt[SMALL], 1, 2, 200, y, ":")
            else:
                c.text(fnt[SMALL], 1, 2, 200, y, v)
        return c

    d = [0, 2, 1, 1, 0, 0, 0, 0]                # the save's defaults
    write("settings_0", settings(0, True, d))
    d2 = list(d)
    d2[2] = 3
    write("settings_1", settings(2, True, d2))
    write("settings_2", settings(8, True, d2))
    write("settings_3", settings(8, False, d2))

    # ---- HI SCORES (HISCOREL): a state is (list type, kind box, laps, track position, row, box, lit)
    pal_h, px_h = bkk(datfile.extract(graf, "HISCOREL.BKK"))
    kind_of = [1, 0, 2]                         # the box ARCADE / CHAMPIONSHIP / TIME ATTACK -> the save's kind
    order = [1, 3, 4, 5, 6, 2, 7]               # the tracks in the order they open: England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel

    def hiscores(name, lap_list, kind, laps, pos, row, widget, lit):
        c = Canvas(px_h)
        dk = bytearray(320 * 200)
        c.text_boxed(fnt[TITLE], 1, 2, 0, 320, 4, at("HI SCORES TABLE", 0))
        texts = [at("HI SCORES TABLE", 1 + lap_list), at("HI SCORES TABLE", 3 + kind), at("HI SCORES TABLE", 6 + laps),
                 at("HI SCORES TABLE", 10 + order[pos] - 1)]
        for w in range(5):
            hot = lit and widget == w
            font, colour = (fnt[SMALL2], 3) if hot else (fnt[SMALL], 1)
            if w == 4:
                c.text_boxed(font, colour, 2, 0, 320, 0x55, at("HI SCORES TABLE", 17))
            else:
                c.text_boxed(font, colour, 2, 160 if w & 1 else 0, 160, 0x2c if w < 2 else 0x41, texts[w])
        k = kind_of[kind]
        rows = lists.rows(k, order[pos], 4 if lap_list else (1 if k == 2 else laps))
        top = 10 if row > 11 else 0 if row < 3 else row - 2
        c.clip_text, c.clip_spr = (0x79, 0xB4), (0x77, 0xB4)
        for n, i in enumerate(range(top, min(top + 6, 15))):
            y = 120 + 12 * n
            time, nm, team, car, replay = rows[i]
            s = "\x01" + (" " if i < 9 else "") + str(i + 1) + ". " + nm + "      " + time_text(time)
            hot = i == row
            c.text(fnt[SMALL2 if hot else SMALL], 3 if hot else 1, 2, 50, y, s)
            c.sprite(spr, 0x55 + (team - 1) * 5 + (car - 1), 134, y - 1)
        if not rows[row][4]:                    # VIEW REPLAY is dark: its box through the table three times, over the text
            for y in range(0x51, 0x51 + 0x12):
                for x in range(0x20, 0x120):
                    dk[y * 320 + x] = 3
        write(name, c, dk)
    # (list type, kind box, laps, track position, chosen row, box, lit): ui_screen.c's check_hiscores() has the keys that lead to each
    hiscores("hs_0", 0, 0, 1, 0, 0, 0, True)
    hiscores("hs_1", 0, 0, 1, 0, 4, 1, True)
    hiscores("hs_2", 0, 1, 1, 0, 4, 1, True)
    hiscores("hs_3", 1, 0, 1, 0, 4, 0, True)
    hiscores("hs_4", 1, 0, 1, 0, 0, 0, True)
    hiscores("hs_5", 1, 0, 1, 1, 0, 3, True)
    hiscores("hs_6", 1, 0, 1, 1, 0, 4, True)
    hiscores("hs_7", 1, 0, 1, 1, 0, 4, False)
    hiscores("hs_8", 0, 0, 1, 0, 14, 0, True)

    # ---- the end of the race: the clocks of the original's screen run in frames of 20 ms (a hundredth of a second is half a frame)
    gspr = sprites(datfile.extract(game1, "GAMEL.SPR"))
    for run, race, cfg, keep, right_at in END_RUNS:
        track, place = cfg["track"], cfg["place"]
        clock, blink, choice, rank, recorded, top, frac = 0, 0x20, 0, -1, False, 0, f32(0.0)
        for j in range(max(keep) + 1):
            if j:
                clock += 2
                blink += 2
            c = Canvas()
            dk = bytearray(320 * 200)
            kind = 2 if cfg.get("attack") else 1
            if clock < 400 and kind == 2:
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 60, gtxt[21])
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 80, time_text(cfg["time"]))
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 100, gtxt[38])
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 120, time_text(cfg["best"] if cfg["best"] > 0 else 3599.999))
            elif clock < 400:
                if place == 1:
                    c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 70, gtxt[5])
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 100, gtxt[place - 1] + " " + gtxt[4])
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 130, time_text(cfg["time"]))
            else:
                lst = 0 if cfg["laps"] == 1 else 1 if cfg["laps"] == 3 else 2 if cfg["laps"] == 5 else 3
                if kind == 2:
                    lst = 1                             # (time attack has only the three-lap totals)
                if not recorded:
                    recorded = True
                    rank = lists.add(kind, track, lst, cfg["time"], "MIL", cfg["team"], cfg["car"])
                    if cfg["best"] > 0:
                        lists.add(kind, track, 4, cfg["best"], "MIL", cfg["team"], cfg["car"])
                if right_at is not None and j == right_at:
                    choice, blink = 1, 0x20
                for y in range(180, 200):
                    for x in range(320):
                        dk[y * 320 + x] = 1
                for y in range(78, 78 + 64):
                    for x in range(28, 288):
                        dk[y * 320 + x] = 1
                sx, sy, w, h, _ = gspr[0x27 + track - 1]
                c.sprite(gspr, 0x27 + track - 1, (320 - (w + sx)) // 2 - (sx - (1 if sx < 0 else 0)) // 2, 4)
                for k in range(2):
                    lit = choice == k and blink & 0x20
                    c.text_boxed(fnt[SMALL2 if lit else SMALL], 3 if lit else 1, 2, 160 * k, 160, 184, gtxt[12 + k])
                y0 = int(f32(80.0 - f32(12.0 * frac)))
                c.clip_text, c.clip_spr = (0x51, 0x8C), (0x50, 0x8C)
                rows = lists.rows(kind, track, lst)
                for n, i in enumerate(range(top, min(top + 6, 15))):
                    time, nm, team, car, _r = rows[i]
                    s = "\x01" + (" " if i < 9 else "") + str(i + 1) + ". " + nm + "      " + time_text(time)
                    c.text(fnt[SMALL2 if i == rank else SMALL], 3 if i == rank else 1, 2, 50, y0 + 12 * n, s)
                    c.sprite(spr, 0x55 + (team - 1) * 5 + (car - 1), 134, y0 + 12 * n - 1)
                target = 0 if rank < 0 else max(0, rank - 2)
                if target >= 15:
                    target = 10
                if top < target and clock >= 450:
                    frac = f32(frac + f32(0.08))
                    if frac > 1.0:
                        frac, top = f32(0.0), top + 1
            if j in keep:
                ov = bytearray(64000)
                for i in range(64000):
                    ov[i] = 1 if c.px[i] else 0
                write("end_%s_%d" % (run, j), c, dk, ov)


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
            tw = c.text_width(fnt[int(f)], int(sp), txt)        # (the engine's measure, 0xc6c88: no gap after the last character)
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
    pages(game, out, graf, container(game, "GFX/GAME1.DAT"), spr, fnt)
    import zipfile
    with zipfile.ZipFile(os.path.join(out, "graf_deflated.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(game, "GFX", "GRAF1.DAT"), "GFX/GRAF1.DAT")
        z.write(os.path.join(game, "DATA", "LOCAL.DAT"), "DATA/LOCAL.DAT")
        z.write(os.path.join(game, "GFX", "GAME1.DAT"), "GFX/GAME1.DAT")
    import ui_arcade_ref
    helpers = {"bkk": bkk, "container": container, "datfile": datfile, "Canvas": Canvas, "sprites": sprites}
    n_runs, n_frames = ui_arcade_ref.build(game, graf, spr, fnt, out, helpers)
    import ui_league_ref
    l_runs, l_frames, l_ends = ui_league_ref.build(game, graf, spr, fnt, out, helpers)
    print(f"{len(lines)} expectations, {len(cases)} cases, 3 frames, {n_runs} arcade page runs with {n_frames} frames, {l_runs} championship page runs with {l_frames} frames "
          f"and {l_ends} end screens -> {out}")


if __name__ == "__main__":
    main()
