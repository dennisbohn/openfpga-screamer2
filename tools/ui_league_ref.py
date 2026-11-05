"""The championship pages' references for tools/menu_screens.sh (firmware/s2leaguepages.c, s2end.c's championship screen): an independent model of what the
original's pages draw and do, written from the game's files and from a reading of STARTL.EXE (docs/screamer2/menus.md row 6; the program's pages 0x4337d LEAGUE
SELECTION, 0x39569 WELCOME TO LEAGUE n, 0x43ade LEAGUE REPORT with its prompt of the TRACK page 0x3ee28, and 0x48041 the championship's end screen, read with
tools/dis.sh), not from the firmware. The rules of the league (the table of leagues, the points, the standings' sort, what winning opens) are written here again
from the program's code, not taken from firmware/s2league.c.

Called by tools/ui_reference.py: build(...) writes into the reference folder
  league.txt            one line per scripted run of a page: "case <name> <page> <league> <races done> <points 0..3> <team> <name> <league open> <tracks open>
                        <fifth cars (a bit a team)> <script> <result> <calls> <clicks> <league open after> <tracks open after> <fifth cars after> <frames> <league after>"
                        page 0 LEAGUE SELECTION (the league field is the cursor's start; the races done and the points are not used), 1 WELCOME TO LEAGUE n, 2 LEAGUE
                        REPORT; the script: one character a frame (. none, U D L R A B S s X, < > the shoulders); the result (1 / -1, and the report's 2 next race,
                        3 league over, 4 left the league), the harness's wait_swap_done() calls, the menu sounds in order (- none), the frames to compare
  lg_<case>_<tick>      .idx the frame before its darkening (the picture and what is drawn under the dark box), .dk the passes of darkening (the box, 0 or 1), .b what is
                        drawn over the darkened box (0: nothing), .pal the palette as the fade leaves it
  leagueend.txt, lend_* the championship's end screen over a race picture: "run <name> <race picture> <track> <laps> <player's place> <time> <best lap> <team> <car> <the
                        four cars: name team car place done time> <frames kept>"; lend_<run>_<j>.idx / .dk / .ov as the arcade end screen's (ui_reference.py)
"""
import os
import struct

FADE = 25
FULL = 0x8000
STEP = (FULL + FADE - 1) // FADE
# the table at 0x1130b0, seven numbers a league: races, tracks opened, fifth car, next league, the computers' level, two of the weather's (not needed)
LEAGUES = [(4, 1, 0, 1, 2), (5, 2, 0, 2, 1), (6, 3, 0, 3, 0), (6, 0, 1, 0, 1)]
POINTS = [10, 6, 3, 0]                  # 0x113180
MARKS = 0x51                            # MENUL.SPR: the teams' marks of the report
TITLE, MEDIUM, SMALL, SMALL2 = 0, 1, 2, 3


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def scaled(pal, k):
    if k >= FULL:
        return [c & 0xFFFFFF for c in pal]
    return [((c >> 16 & 255) * k >> 15) << 16 | ((c >> 8 & 255) * k >> 15) << 8 | (c & 255) * k >> 15 for c in pal]


def time_text(t):
    t = f32(t)
    if not t > 0 or t > 3599.9:
        return "--:--:--"
    whole = int(t)
    return "%02d:%02d:%02d" % (whole // 60 % 100, whole % 60, int((t - whole) * 100.0))


class Ref:
    def __init__(self, game, graf, spr, fnt, out, helpers):
        self.spr, self.fnt, self.out, self.h = spr, fnt, out, helpers
        bkk, container, datfile = helpers["bkk"], helpers["container"], helpers["datfile"]
        self.pics = {n: bkk(datfile.extract(graf, n)) for n in ("BKG_3L.BKK", "LEAG_1L.BKK", "LEAG_2L.BKK", "LEAG_3L.BKK", "LEAG_4L.BKK")}
        loc = container(game, "DATA/LOCAL.DAT")
        self.lines = [l for l in datfile.extract(loc, "STRINGS.TXT").decode("latin-1").replace("\r", "").split("\n") if l and not l.startswith("*")]
        self.game = [l for l in datfile.extract(loc, "GAME.TXT").decode("latin-1").replace("\r", "").split("\n") if l and not l.startswith("*")]
        self.cases, self.nframes = [], 0

    def at(self, anchor, k=0):
        return self.lines[self.lines.index(anchor) + k]

    def canvas(self, name=None):
        c = self.h["Canvas"]()
        if name:
            c.px[:] = self.pics[name][1]
        return c

    def boxed(self, c, font, colour, x0, w, y, txt):
        c.text_boxed(self.fnt[font], colour, 2, x0, w, y, txt)

    def centred(self, c, font, colour, y, txt):
        self.boxed(c, font, colour, 0, 320, y, txt)

    @staticmethod
    def dark_box(dk, x, y, w, h):
        """the original's 0x51868 works in words of four pixels: x and the width lose their low two bits"""
        x &= ~3
        for yy in range(y, min(200, y + h)):
            for xx in range(x, min(320, x + (w & ~3))):
                dk[yy * 320 + xx] = 1

    # ---------------------------------------------------------------------------------------------------------- LEAGUE SELECTION (0x4337d)
    def select(self, st):
        ref, s = self, self.spr
        maxopen = st["lopen"]

        class M:
            pic = "BKG_3L.BKK"

            def __init__(m):
                m.t0, m.first = 0, 0

            def key(m, tick, keys):
                if keys & {"A", "s"}:
                    return -1
                if "L" in keys and st["league"] > 0:
                    st["league"] -= 1
                    m.t0 = tick
                    st["clicks"].append(0)
                elif "R" in keys and st["league"] < 3:
                    st["league"] += 1
                    m.t0 = tick
                    st["clicks"].append(0)
                if keys & {"B", "S"} and st["league"] <= maxopen:
                    st["clicks"].append(6)
                    return 1
                return 0

            def draw(m, tick):
                cur = st["league"]
                while cur - m.first >= 2:
                    m.first += 1
                while cur - m.first < 0:
                    m.first -= 1
                slot, clock = cur - m.first, (tick - m.t0) * 2
                a, b, dk = ref.canvas(m.pic), ref.canvas(), bytearray(64000)
                a.sprite(s, 44 + m.first, 19, 39)
                a.sprite(s, 44 + m.first + 1, 176, 39)
                if (clock & 0x3F) < 0x20:
                    a.sprite(s, 48, 12 + 157 * slot, 32)
                for i in range(2):
                    league = m.first + i
                    if league > maxopen:
                        ref.boxed(a, SMALL, 1, 160 * i, 160, 100, ref.at("NOT", 0))
                        ref.boxed(a, SMALL, 1, 160 * i, 160, 112, ref.at("NOT", 1))
                    ref.boxed(a, SMALL, 1, 160 * i, 160, 160, ref.at("LEAGUE 1", 2 * league))
                    ref.boxed(a, SMALL, 1, 160 * i, 160, 172, ref.at("LEAGUE 1", 2 * league + 1))
                ref.dark_box(dk, abs(slot - 1) * 0x9D + 0x12, 0x23, 0x86, 0x9C)
                if (clock & 0x7F) < 0x40:
                    if m.first != 0:
                        b.sprite(s, 3, 10, 172)
                    if m.first < 2:
                        b.sprite(s, 4, 298, 172)
                ref.centred(b, TITLE, 1, 2, ref.at("LEAGUE SELECTION"))
                return a, dk, b
        return M()

    # ---------------------------------------------------------------------------------------------------------- WELCOME TO LEAGUE n (0x39569)
    def welcome(self, st):
        ref = self

        class M:
            pic = "LEAG_%dL.BKK" % (st["league"] + 1)

            def key(m, tick, keys):
                if keys & {"A", "s"}:
                    return -1
                return 1 if keys & {"B", "S"} else 0

            def draw(m, tick):
                a, b, dk = ref.canvas(m.pic), ref.canvas(), bytearray(64000)
                ref.dark_box(dk, 0, 0, 320, 0x2A)
                ref.centred(b, MEDIUM, 1, 2, ref.at("WELCOME TO"))
                ref.centred(b, MEDIUM, 1, 20, ref.at("LEAGUE 1", 2 * st["league"]))
                return a, dk, b
        return M()

    # ---------------------------------------------------------------------------------------------------------- LEAGUE REPORT (0x43ade)
    def report(self, st):
        ref, s = self, self.spr
        pts = st["points"]
        races = LEAGUES[st["league"]][0]
        # the standings: the program's sort (0x3592e) swaps a pair only when the first has fewer points, which a stable sort by the points, the most first, does too
        order = sorted(range(4), key=lambda i: -pts[i])
        team_of = [(st["team"] - 1 + k) % 4 for k in range(4)]            # the cars' teams (0x339e7: the player's team + k)
        name = st["name"].rstrip(" ")

        class M:
            pic = "LEAG_%dL.BKK" % (st["league"] + 1)

            def __init__(m):
                m.state, m.ask, m.yes, m.t0 = 0, False, False, 0

            def key(m, tick, keys):
                if m.ask:
                    if "L" in keys:
                        m.yes = True
                    elif "R" in keys:
                        m.yes = False
                    if keys & {"A", "s"}:
                        m.ask = False
                    elif keys & {"B", "S"}:
                        if m.yes:
                            return -4
                        m.ask = False
                    return 0
                if keys & {"A", "s"} and m.state == 0 and st["done"] < races and st["done"] > 0:
                    m.ask, m.yes, m.t0 = True, False, tick
                    return 0
                if not keys & {"B", "S"}:
                    return 0
                if m.state == 0:
                    if st["done"] < races:
                        return 2
                    if order[0] == 0:                                       # the player is first: what the league opens (0x44194 .. 0x442c1)
                        _r, tracks, fifth, nxt, _l = LEAGUES[st["league"]]
                        if fifth:
                            st["car5"] |= 1 << (st["team"] - 1)
                        if nxt and nxt > st["lopen"]:
                            st["lopen"] = nxt
                        if tracks:
                            st["topen"] = max(st["topen"], tracks)
                        elif st["car5"] == 15:
                            st["topen"] = 4
                        m.state = 1
                    else:
                        m.state = 2
                    return 0
                return 3

            def draw(m, tick):
                clock = (tick - m.t0) * 2
                a, b, dk = ref.canvas(m.pic), ref.canvas(), bytearray(64000)
                top = a
                if m.state == 0:
                    ref.centred(a, TITLE, 1, 4, ref.at("LEAGUE REPORT"))
                    for k in range(4):
                        car, y = order[k], 0x38 + 0x18 * k
                        a.sprite(s, MARKS + team_of[car], 104, y)
                        txt = "\x01%d.  %s%s%d" % (k + 1, name if car == 0 else "CPU", "  " if pts[car] < 10 else " ", pts[car])
                        ref.centred(a, MEDIUM, 1, y, txt)
                    if races - st["done"]:
                        ref.centred(a, SMALL, 1, 152, ref.at("RACES COMPLETED:") + " %d" % st["done"])
                        ref.centred(a, SMALL, 1, 164, ref.at("RACES COMPLETED:", 1) + " %d" % (races - st["done"]))
                elif m.state == 1:
                    ref.centred(a, MEDIUM, 1, 66, ref.at("CONGRATULATIONS !!!"))
                    ref.centred(a, MEDIUM, 1, 90, ref.at("CONGRATULATIONS !!!", 1))
                    ref.centred(a, MEDIUM, 1, 114, ref.at("LEAGUE 1", 2 * st["league"]))
                else:
                    ref.centred(a, MEDIUM, 1, 66, ref.at("BETTER LUCK"))
                    ref.centred(a, MEDIUM, 1, 90, ref.at("BETTER LUCK", 1))
                if m.ask:
                    ref.dark_box(dk, 0, 0, 320, 200)
                    top = b
                    ref.boxed(b, SMALL, 1, 80, 160, 80, ref.at("LEAVING LEAGUE"))
                    ref.boxed(b, SMALL, 1, 80, 160, 95, ref.at("LEAVING LEAGUE", 1))
                    on = (clock & 0x3F) <= 0x20
                    yes_on, no_on = m.yes and on, (not m.yes) and on
                    ref.boxed(b, SMALL2 if yes_on else SMALL, 3 if yes_on else 1, 80, 80, 115, ref.at("LEAVING LEAGUE", 2))
                    ref.boxed(b, SMALL2 if no_on else SMALL, 3 if no_on else 1, 160, 80, 115, ref.at("LEAVING LEAGUE", 3))
                elif (clock & 0x7F) < 0x40:
                    ref.centred(b, SMALL, 1, 184, ref.at("ACCELERATE TO CONTINUE"))
                return a, dk, b
        return M()

    def run(self, name, page, st, script, frames):
        """a scripted run of one page: the result, the calls the harness will make, the frames; st is changed by the page's logic (the cursor, what a win opens)"""
        st = dict(dict(league=0, done=0, points=[0, 0, 0, 0], team=1, name="MIL", lopen=0, topen=0, car5=0, clicks=[]), **st)
        start = dict(st, points=list(st["points"]))
        model = (self.select(st), self.welcome(st), self.report(st))[page]
        pal = self.pics[model.pic][0]
        want = {int(f): True for f in frames}
        result, fade, tick, calls = 0, FULL, 0, 0
        while True:
            ch = script[tick] if tick < len(script) else "."
            keys = set()
            if ch != "." and (tick == 0 or script[tick - 1] != ch):
                keys.add(ch)
            if not result:
                result = model.key(tick, keys)
                if result < 0:
                    calls = tick + 1                                        # a cut: the page returns before it draws this frame
                    break
            a, dk, b = model.draw(tick)
            if tick in want:
                self.write(name, tick, a.px, dk, b.px, scaled(pal, fade))
            if result:
                fade -= STEP
                if fade <= 0:
                    calls = tick + 1
                    break
            tick += 1
        for t in want:
            assert t <= calls - 2, f"{name}: frame {t} is not seen before the end ({calls} calls)"
        result = -result if result < 0 and page == 2 else result
        clicks = ",".join(str(k) for k in st["clicks"]) or "-"
        show = -1 if result < 0 else result
        self.cases.append(f"case {name} {page} {start['league']} {start['done']} {' '.join(str(p) for p in start['points'])} {start['team']} {start['name'].replace(' ', '_')} "
                          f"{start['lopen']} {start['topen']} {start['car5']} {script} {show} {calls} {clicks} {st['lopen']} {st['topen']} {st['car5']} "
                          f"{','.join(frames) or '-'} {st['league']}")

    def write(self, name, tick, px, dk, b, pal):
        base = os.path.join(self.out, f"lg_{name}_{tick}")
        open(base + ".idx", "wb").write(bytes(px))
        open(base + ".dk", "wb").write(bytes(dk))
        open(base + ".b", "wb").write(bytes(b.px if hasattr(b, "px") else b))
        open(base + ".pal", "wb").write(b"".join(struct.pack("<I", c) for c in pal))
        self.nframes += 1


END_RUNS = [
    # name, race picture, track, laps, the player's place and time, best lap, team, car, the four cars: (name, team, car, place, done, time)
    ("L1", "s2_track4_city", 4, 3, 1, 85.5, 29.5, 2, 3,
     [("MIL", 2, 3, 1, 1, 85.5), ("CPU", 3, 4, 2, 1, 88.25), ("CPU", 4, 1, 3, 0, 0.0), ("CPU", 1, 2, 4, 0, 0.0)], [100, 199, 200, 231, 450]),
    ("L2", "s2_track1_village", 1, 3, 3, 140.125, 45.0, 4, 2,
     [("MIL", 4, 2, 3, 1, 140.125), ("CPU", 1, 3, 1, 1, 121.0), ("CPU", 2, 4, 2, 1, 130.5), ("CPU", 3, 5, 4, 0, 0.0)], [100, 400]),
    ("L3", "s2_track5_snow", 7, 1, 2, 99.0, 0.0, 1, 1,
     [("A", 1, 1, 2, 1, 99.0), ("CPU", 2, 2, 1, 1, 90.0), ("CPU", 3, 3, 3, 1, 101.5), ("CPU", 4, 4, 4, 1, 120.0)], [400]),
    # menus row 10: the race can be run again (names R...): REPLAY and CONTINUE in halves, REPLAY first; R2 presses RIGHT in frame 300: CONTINUE lit; P1 is the replay's own screen
    ("R1", "s2_track4_city", 4, 3, 1, 85.5, 29.5, 2, 3,
     [("MIL", 2, 3, 1, 1, 85.5), ("CPU", 3, 4, 2, 1, 88.25), ("CPU", 4, 1, 3, 0, 0.0), ("CPU", 1, 2, 4, 0, 0.0)], [100, 231, 450]),
    ("R2", "s2_track1_village", 1, 3, 3, 140.125, 45.0, 4, 2,
     [("MIL", 4, 2, 3, 1, 140.125), ("CPU", 1, 3, 1, 1, 121.0), ("CPU", 2, 4, 2, 1, 130.5), ("CPU", 3, 5, 4, 0, 0.0)], [231, 302, 400]),
    ("P1", "s2_track4_city", 4, 3, 1, 85.5, 29.5, 2, 3,
     [("MIL", 2, 3, 1, 1, 85.5), ("CPU", 3, 4, 2, 1, 88.25), ("CPU", 4, 1, 3, 0, 0.0), ("CPU", 1, 2, 4, 0, 0.0)], [231, 400]),
]


class End:
    """the championship's end screen (0x48041): what it draws over the race, one frame at a clock and a blink (in hundredths of a second: two a frame of 20 ms)"""
    def __init__(self, game, game1, spr, fnt, helpers):
        datfile, container = helpers["datfile"], helpers["container"]
        self.Canvas, self.spr, self.fnt = helpers["Canvas"], spr, fnt
        self.gspr = helpers["sprites"](datfile.extract(game1, "GAMEL.SPR"))
        loc = container(game, "DATA/LOCAL.DAT")
        self.gtxt = [l for l in datfile.extract(loc, "GAME.TXT").decode("latin-1").replace("\r", "").split("\n") if l and not l.startswith("*")]

    def draw(self, track, place, ptime, cars, clock, blink, replay=False, choice=0):
        """(the canvas of text and sprites, the passes of darkening) of a frame; cars: (name, team, car, place, done, time) of the four"""
        c, dk, fnt, gtxt = self.Canvas(), bytearray(64000), self.fnt, self.gtxt
        n = len([k for k in cars if k[4]])
        y0 = 81 - 5 * (n - 1)
        if clock < 400:
            if place == 1:
                c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 70, gtxt[5])
            c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 100, gtxt[place - 1] + " " + gtxt[4])
            c.text_boxed(fnt[MEDIUM], 1, 2, 0, 320, 130, time_text(ptime))
            return c, dk
        for y in range(180, 200):                                           # the band, and the box in words of four pixels (x 6 is 4)
            for x in range(320):
                dk[y * 320 + x] = 1
        for y in range(y0 - 2, y0 - 2 + 13 * n + 14):
            for x in range(4, 4 + 312):
                dk[y * 320 + x] = 1
        sx, sy, w, h, _ = self.gspr[0x27 + track - 1]
        c.sprite(self.gspr, 0x27 + track - 1, (320 - (w + sx)) // 2 - (sx - (1 if sx < 0 else 0)) // 2, 4)
        lit = bool(blink & 0x20)
        if replay:                                                          # REPLAY and CONTINUE in halves (0x4849f ..)
            for k in range(2):
                on = lit and choice == k
                c.text_boxed(fnt[SMALL2 if on else SMALL], 3 if on else 1, 2, 160 * k, 160, 184, gtxt[11 if k == 0 else 15])
        else:
            c.text_boxed(fnt[SMALL2 if lit else SMALL], 3 if lit else 1, 2, 0, 320, 184, gtxt[15])
        c.text(fnt[SMALL], 1, 2, 38, y0, gtxt[16])
        for (nm, t, cr, pl, dn, tm) in cars:
            if not dn:
                continue
            y = y0 + 14 + 12 * (pl - 1)
            pts = POINTS[pl - 1]
            row = "\x01%d. %s        %s%s%d" % (pl, nm, time_text(tm), "  " if pts < 10 else " ", pts)
            c.text(fnt[SMALL], 1, 2, 38, y, row)
            c.sprite(self.spr, 0x55 + (t - 1) * 5 + (cr - 1), 113, y - 1)
        return c, dk


def end_screens(game, out, graf, game1, spr, fnt, helpers):
    """the championship's end of a race over a race picture, frame by frame for the harness: the clocks run in frames of 20 ms"""
    end = End(game, game1, spr, fnt, helpers)
    lines = []
    for name, race, track, laps, place, ptime, best, team, car, cars, keep in END_RUNS:
        clock, blink, choice = 0, 0x20, 0
        replay = name[0] in "RP"
        lines.append("run %s %s %d %d %d %r %r %d %d %s %s" % (name, race, track, laps, place, ptime, best, team, car,
                                                                 " ".join("%s %d %d %d %d %r" % c for c in cars), ",".join(str(k) for k in keep)))
        for j in range(max(keep) + 1):
            if j:
                clock += 2
                blink += 2
            if name == "R2" and j == 300:
                choice, blink = 1, 0x20
            c, dk = end.draw(track, place, ptime, cars, clock, blink, replay, choice)
            if j in keep:
                ov = bytearray(64000)
                for i in range(64000):
                    ov[i] = 1 if c.px[i] else 0
                for ext, data in (("idx", c.px), ("dk", dk), ("ov", ov)):
                    open(os.path.join(out, "lend_%s_%d.%s" % (name, j, ext)), "wb").write(bytes(data))
    open(os.path.join(out, "leagueend.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    return len(END_RUNS)


def S(spec):
    """a script from "<frame>:<key> ...": the key's character in that frame, a dot in the others"""
    keys = [(int(a), b) for a, b in (w.split(":") for w in spec.split())]
    out = ["."] * (max(t for t, _ in keys) + 1)
    for t, k in keys:
        out[t] = k
    return "".join(out)


def build(game, graf, spr, fnt, out, helpers):
    r = Ref(game, graf, spr, fnt, out, helpers)
    # LEAGUE SELECTION: league 1 open only, the cursor on it: the frame blinks (0x20 hundredths on, 0x20 off), RIGHT moves to the closed league 2 (the window stays), ENTER on it does
    # nothing, LEFT back, ENTER goes on after the fade; leagues 1 to 4 open: RIGHT x 3 scrolls the window and the arrows change; ESC is a cut
    r.run("sel_first", 0, dict(league=0, lopen=0), S("30:R 61:B 65:L 86:B"), ["0", "15", "16", "20", "31", "32", "33", "70", "90", "100"])
    r.run("sel_walk", 0, dict(league=0, lopen=3), S("5:R 11:R 20:R 41:L 47:L 60:B"), ["1", "6", "12", "20", "40", "52", "60", "80"])
    r.run("sel_start3", 0, dict(league=3, lopen=3), S("3:L 14:A"), ["2", "5", "12"])
    r.run("sel_two", 0, dict(league=1, lopen=1), S("3:R 10:R 17:B 22:L 28:L 36:B"), ["2", "8", "10", "16", "22", "30", "40"])
    r.run("sel_keys", 0, dict(league=0, lopen=2), S("2:S"), ["1", "20", "25"])
    r.run("sel_esc", 0, dict(league=2, lopen=2), S("1:A"), ["0"])
    # WELCOME TO LEAGUE n, each league: ENTER fades, ESC is a cut
    for lg in range(4):
        r.run("welcome_%d" % (lg + 1), 1, dict(league=lg), S("5:B"), ["0", "3"])
    r.run("welcome_esc", 1, dict(league=1), S("2:A"), ["1"])
    # the report: after race 1 of league 1 (the player first, 10 points), a tie for the lead in league 2 (the player ahead: the order of the cars), ESC asks LEAVING LEAGUE
    # (NO first, LEFT YES, RIGHT NO, ESC hides it, YES leaves, ENTER on NO hides it), the last race of league 1 won, of league 3 lost, a tie for the lead won, league 4 won
    # with the other three teams' fifth cars already won (the seventh track opens) and with only one
    r.run("rep_one", 2, dict(league=0, done=1, points=[10, 6, 3, 0], team=1, name="MIL"), S("40:B"), ["0", "15", "16", "31", "32", "39"])
    r.run("rep_ties", 2, dict(league=1, done=2, points=[16, 16, 16, 0], team=3, name="A"), S("5:B"), ["1", "3"])
    r.run("rep_ask", 2, dict(league=1, done=2, points=[13, 16, 9, 6], team=2, name="ZED"),
          S("3:A 24:L 45:R 49:A 53:A 57:L 61:B"), ["2", "6", "20", "27", "40", "47", "50", "55", "58", "59"])
    r.run("rep_ask_no", 2, dict(league=2, done=1, points=[3, 10, 6, 0], team=4, name="MIL"), S("3:A 9:B 15:B"), ["2", "5", "8", "12"])
    r.run("rep_last_won", 2, dict(league=0, done=4, points=[34, 20, 12, 5], team=2, name="MIL", lopen=0, topen=0), S("5:B 36:B"), ["3", "10", "40"])
    r.run("rep_last_lost", 2, dict(league=2, done=6, points=[30, 40, 14, 3], team=1, name="MIL", lopen=2, topen=2), S("5:B 36:B"), ["3", "10", "40"])
    r.run("rep_last_tie", 2, dict(league=1, done=5, points=[40, 40, 5, 5], team=1, name="MIL", lopen=1, topen=1), S("5:B 14:B"), ["3", "10"])
    r.run("rep_league4", 2, dict(league=3, done=6, points=[50, 20, 12, 6], team=3, name="MIL", lopen=3, topen=3, car5=0b1011), S("5:B 14:B"), ["3", "10"])
    r.run("rep_league4_some", 2, dict(league=3, done=6, points=[50, 20, 12, 6], team=1, name="MIL", lopen=3, topen=3, car5=0b0001), S("5:B 14:B"), ["3", "10"])
    r.run("rep_esc_last", 2, dict(league=1, done=5, points=[40, 20, 5, 5], team=1, name="MIL"), S("3:A 9:B 18:B"), ["2", "6", "12"])
    open(os.path.join(out, "league.txt"), "w", newline="\n").write("\n".join(r.cases) + "\n")
    ends =end_screens(game, out, graf, helpers["container"](game, "GFX/GAME1.DAT"), spr, fnt, helpers)
    return len(r.cases), r.nframes, ends
