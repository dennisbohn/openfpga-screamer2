"""The arcade pages' references for tools/menu_screens.sh (firmware/s2arcade.c): an independent model of what the original's pages draw and do,
written from the game's files and from a reading of STARTL.EXE (docs/screamer2/menus.md "The arcade pages"; the program's pages
0x3b6e8, 0x41a69, 0x40c31, read with tools/dis.sh), not from the firmware.

Called by tools/ui_reference.py (which has the readers of the game's files): build(...) writes into the reference folder
  arcade.txt              one line per scripted run of a page: "case <name> <page> <track> <team> <car> <manual> <unlock> <nopen> <car5> <script>
                          <result> <final track> <final team> <final car> <final manual> <calls> <clicks> <frames>"
                          page 0 title, 1 track, 2 car, 3 transmission; the start state; unlock / nopen / car5: what the save has open (unlock 1: all
                          of it); the script: one character a frame (. none, U D L R A B S s X the buttons, < > the shoulder buttons L R);
                          the result (1 ENTER, -1 ESC), the state the page leaves, how many times the harness calls wait_swap_done() (one per
                          frame, and the one that ends the page), the menu sounds in the order they are played (- none), the frames to compare
                          (a frame is the picture on show at that call: the tick's, 0 the first; * = also a picture to look at)
  arc_<case>_<tick>.idx   that frame's 320 x 200 palette indices, .pal its palette (256 x 0x00RRGGBB as the fade leaves it)
"""
import os
import struct

ORDER = [1, 3, 4, 5, 6, 2, 7]       # the tracks in the original's order: STARTL.EXE's table at 0x113224 holds 0 2 3 4 5 1 6 (files' track - 1)
FADE = 25                           # frames of a fade out: 2.0 x the frame time 0.02 added to 0..1 each frame (0x4189f.. 0x11153e)
FULL = 0x8000
STEP = (FULL + FADE - 1) // FADE    # what the page runner takes off 0x8000 a frame


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def scaled(pal, k):
    """the palette as the page runner shows it at fade k of 0x8000 (main.c pal_faded)"""
    if k >= FULL:
        return [c & 0xFFFFFF for c in pal]
    return [((c >> 16 & 255) * k >> 15) << 16 | ((c >> 8 & 255) * k >> 15) << 8 | (c & 255) * k >> 15 for c in pal]


class Ref:
    def __init__(self, game, graf, spr, fnt, out, helpers):
        self.spr, self.fnt, self.out, self.h = spr, fnt, out, helpers
        bkk, container, datfile = helpers["bkk"], helpers["container"], helpers["datfile"]
        self.pics = {n: bkk(datfile.extract(graf, n)) for n in ("ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK")}
        loc = container(game, "DATA/LOCAL.DAT")
        lines = [l.rstrip(" \r") for l in datfile.extract(loc, "STRINGS.TXT").decode("latin-1").split("\n")]
        self.lines = [l for l in lines if l and not l.startswith("*")]
        # the program draws line 15 of the table at 0x127ed8 (the pointer at 0x127f14) on the title page: the prompt
        assert self.lines[15] == "ACCELERATE TO CONTINUE", self.lines[15]
        self.team_names = self.lines[1:5]
        self.cases, self.nframes = [], 0

    def canvas(self, name):
        c = self.h["Canvas"]()
        c.px[:] = self.pics[name][1]
        return c

    def centred(self, c, font, colour, space, y, txt):
        w = c.text_width(self.fnt[font], space, txt)    # (the engine's measure, 0xc6c88: no gap after the last character)
        c.text(self.fnt[font], colour, space, (320 - w) >> 1, y, txt)

    # --- the pages: an object with key(tick, buttons that went down) -> result and draw(tick, canvas)
    def title(self, state):
        ref = self

        class M:
            def key(m, tick, keys):
                if keys & {"B", "S"}:
                    state["clicks"].append(1)
                return -1 if keys & {"A", "s"} else 1 if keys & {"B", "S"} else 0

            def draw(m, tick, c):
                if ((tick * 2) & 0x7F) < 0x40:
                    ref.centred(c, 2, 1, 2, 184, ref.lines[15])
        return M()

    def track(self, state, nopen):
        s = self.spr

        class M:
            def __init__(m):
                m.n = nopen
                pos = ORDER.index(state["track"])
                m.pos = pos if pos < nopen else 0
                state["track"] = ORDER[m.pos]
                m.scroll = m.frame = 0.0
                m.t0 = 0
                m.force = None                  # (frame shown, arrows shown) instead of the blinking: for comparing with a capture of unknown phase

            def move(m, tick, pos):
                m.pos, m.t0 = pos, tick
                state["track"] = ORDER[pos]
                state["clicks"].append(0)

            def key(m, tick, keys):
                if keys & {"A", "s"}:
                    return -1
                if keys & {"B", "S"}:
                    state["clicks"].append(2)
                    return 1
                if "L" in keys and m.pos > 0:
                    m.move(tick, m.pos - 1)
                elif "R" in keys and m.pos < m.n - 1:
                    m.move(tick, m.pos + 1)
                elif keys & {"<", ">"}:
                    m.move(tick, (m.pos + (3 if ">" in keys else -3)) % m.n)
                return 0

            rate = 1                            # frames' lengths in 20 ms steps: the original eases by 0.75 ^ (50 x the frame's time), 0.75 at 50 pictures a second

            def ease(m, v, to, or_equal):
                v = f32(f32(f32(v - to) * f32(0.75 ** m.rate)) + to)
                d = abs(to - v)
                return to if (d <= 0.5 if or_equal else d < 0.5) else v

            def draw(m, tick, c):
                last, pos = m.n - 1, m.pos
                first = 0 if pos == 0 else pos - 1 if last - pos >= 1 else pos - 2
                slot = 0 if pos == 0 else 2 if pos == last else 1
                clock = (tick - m.t0) * 2
                c.sprite(s, 105, 28, 15)
                to = float(8 - 105 * first)
                m.scroll = to if m.scroll == 0.0 else m.ease(m.scroll, to, True)
                for i in range(last + 1):
                    x = int(f32(float(i * 105) + m.scroll))
                    if x >= 320 or x + 92 <= 0:
                        continue
                    c.sprite(s, (5 if i == pos else 12) + ORDER[i] - 1, x, 64)
                if (clock & 0x7F) < 0x40 if m.force is None else m.force[1]:
                    if first > 0:
                        c.sprite(s, 1, 10, 170)
                    if first + 2 < last:
                        c.sprite(s, 2, 298, 170)
                to = float(105 * slot + 2)
                m.frame = to if m.frame == 0.0 else m.ease(m.frame, to, False)
                if (clock & 0x3F) < 0x20 if m.force is None else m.force[0]:
                    c.sprite(s, 0, int(m.frame), 60)
        return M()

    def car(self, state, car5):
        s, ref = self.spr, self

        def cars():
            return 5 if car5 else 4

        class M:
            def key(m, tick, keys):
                if keys & {"A", "s"}:
                    return -1
                if keys & {"B", "S"}:
                    return 1
                dx, dy = ("R" in keys) - ("L" in keys), ("D" in keys) - ("U" in keys)
                if dx:
                    state["car"] = (state["car"] - 1 + dx) % cars() + 1
                    state["clicks"].append(4)
                elif dy:
                    state["team"] = (state["team"] - 1 + dy) % 4 + 1
                    if state["car"] > cars():
                        state["car"] = 1
                    state["clicks"].append(4)
                return 0

            def draw(m, tick, c):
                team, car = state["team"], state["car"]
                c.sprite(s, 34, 28, 15)
                c.sprite(s, 35 + team - 1, 226, 1)
                c.sprite(s, 30 + team - 1 if car == 5 else 26 + car - 1, 18, 155)
                ref.centred(c, 0, 1, 2, 62, ref.team_names[team - 1])
                ref.centred(c, 1, 2, 2, 100, f"{car} / {cars()}")
                ref.centred(c, 2, 0, 2, 124, "UP / DOWN: TEAM    LEFT / RIGHT: CAR")
        return M()

    def trans(self, state):
        s = self.spr

        class M:
            def __init__(m):
                m.t0 = 0

            def key(m, tick, keys):
                if keys & {"A", "s"}:
                    return -1
                if keys & {"B", "S"}:
                    state["clicks"].append(3)
                    return 1
                if "L" in keys and state["manual"]:
                    state["manual"] = 0
                elif "R" in keys and not state["manual"]:
                    state["manual"] = 1
                else:
                    return 0
                m.t0 = tick
                state["clicks"].append(0)
                return 0

            def draw(m, tick, c):
                manual, lit = state["manual"], int(not (((tick - m.t0) * 2) & 0x20))
                c.sprite(s, 39, 28, 15)
                c.sprite(s, 35 + state["team"] - 1, 226, 1)
                c.sprite(s, 40 + int(not manual and lit), 18, 155)
                c.sprite(s, 42 + int(manual and lit), 159, 155)
        return M()

    def run(self, name, page, start, script, frames, nopen=7, unlock=1, car5=0):
        """a scripted run of one page through its model: the result, the state it leaves, the calls the harness will make, the frames"""
        state = dict(start, clicks=[])
        pic = ("ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK", "BKG_2L.BKK")[page]
        model = (self.title(state), self.track(state, nopen), self.car(state, 1 if unlock else car5), self.trans(state))[page]
        pal = self.pics[pic][0]
        want = {int(f.rstrip("*")): f.endswith("*") for f in frames}
        result, fade, tick, calls = 0, FULL, 0, 0
        while True:
            ch = script[tick] if tick < len(script) else "."
            keys = set()
            if ch != "." and (tick == 0 or script[tick - 1] != ch):             # a press: the button went down in this frame
                keys.add(ch)
            if not result:
                result = model.key(tick, keys)
                if result < 0 or (result and page == 2):                        # (the car page's ENTER goes on to the transmission, a cut as well)
                    calls = tick + 1                                            # a cut: the page returns before it draws this frame
                    break
            c = self.canvas(pic)
            model.draw(tick, c)
            if tick in want:
                self.write(name, tick, c.px, scaled(pal, fade))
            if result:
                fade -= STEP
                if fade <= 0:
                    calls = tick + 1
                    break
            tick += 1
        for t in want:
            assert t <= calls - 2, f"{name}: frame {t} is not seen before the end ({calls} calls)"
        clicks = ",".join(str(k) for k in state["clicks"]) or "-"
        self.cases.append(f"case {name} {page} {start['track']} {start['team']} {start['car']} {start['manual']} {unlock} {nopen} {car5} {script} "
                          f"{result} {state['track']} {state['team']} {state['car']} {state['manual']} {calls} {clicks} "
                          f"{','.join(frames) or '-'}")

    def write(self, name, tick, px, pal):
        base = os.path.join(self.out, f"arc_{name}_{tick}")
        open(base + ".idx", "wb").write(bytes(px))
        open(base + ".pal", "wb").write(b"".join(struct.pack("<I", c) for c in pal))
        self.nframes += 1


def build(game, graf, spr, fnt, out, helpers):
    r = Ref(game, graf, spr, fnt, out, helpers)
    start = {"track": 1, "team": 1, "car": 1, "manual": 0}
    # the title: the prompt blinks (64 hundredths on, 64 off), ENTER fades out over 25 frames, ESC is a cut
    r.run("title_blink", 0, start, "." * 70 + "B", ["0*", "31", "32", "33", "63", "64", "65", "40*"])
    r.run("title_esc", 0, start, "....A", ["3"])
    # the track page from track 1: the frame blinks (16 frames on, 16 off), RIGHT moves it (the frame first, the thumbnails ease)
    r.run("track_first", 1, start, "." * 20 + "R" + "." * 10 + "B", ["0*", "15", "16", "31", "32", "20", "21", "22", "23", "24", "26", "28", "30"])
    # a walk along the tracks: RIGHT four times (the window scrolls from the second place on), the blink restarts at each press
    walk = "".join("R" + "." * 7 for _ in range(4)) + "." * 40 + "B"
    r.run("track_walk", 1, start, walk, ["1", "2", "3", "5", "8", "9", "10", "12", "17", "20", "26", "30", "34*", "50*", "60*"])
    # to the last place: 6 presses (the arrow on the right goes out, the left one stays), then RIGHT again does nothing
    end = "".join("R" + "." * 4 for _ in range(7)) + "." * 60 + "B"
    r.run("track_end", 1, start, end, ["10", "30", "50", "70", "89*", "90", "91", "92", "95", "100"])
    # back to the start with LEFT, LEFT at the start does nothing
    back = "".join("L" + "." * 4 for _ in range(7)) + "." * 50 + "B"
    r.run("track_back", 1, {"track": 7, "team": 1, "car": 1, "manual": 0}, back, ["2", "20", "40", "60", "80*", "100"])
    # the shoulder buttons: R three on (round), L three back (round)
    r.run("track_shoulders", 1, start, ".>." + "." * 30 + ">" + "." * 30 + ">" + "." * 40 + "<" + "." * 40 + "B", ["20*", "60", "100*", "140", "150", "165"])
    # ESC leaves at once, without a fade, and the choice stays
    r.run("track_esc", 1, start, ".R.....R.....A", ["6", "12"])
    # a track that is not open is left for the first; three open tracks: the window never moves and there is no arrow
    r.run("track_three", 1, {"track": 5, "team": 1, "car": 1, "manual": 0}, "....." + "R" + "." * 20 + "R" + "." * 20 + "R" + "." * 20 + "B",
          ["3*", "30", "50*", "70", "90"], nopen=3, unlock=0)
    r.run("track_five", 1, {"track": 6, "team": 1, "car": 1, "manual": 0}, "." * 10 + "R" + "." * 20 + "L" + "." * 30 + "B", ["5", "20", "40*", "70"],
          nopen=5, unlock=0)
    # the car page (the stand-in): left / right choose the car round, up / down the team
    r.run("car_keys", 2, start, "..R.....R.....L.....D.....U.....B", ["0*", "3", "12", "20", "27", "31"])
    r.run("car_bonus", 2, {"track": 1, "team": 3, "car": 4, "manual": 0}, "..R.....R.....D.....B", ["1", "8", "14*", "19"])
    r.run("car_four", 2, {"track": 1, "team": 2, "car": 4, "manual": 0}, "..R.....R.....B", ["1", "8*"], unlock=0, car5=0)
    r.run("car_esc", 2, start, "..L....A", ["2", "5"])
    # the transmission page: the chosen one is lit while bit 0x20 of the hundredths is clear, the other always dim; a choice restarts the blink
    r.run("trans_blink", 3, start, "." * 40 + "R" + "." * 20 + "B", ["0*", "15", "16", "31", "32", "47", "48", "40", "41", "42", "50", "56", "57", "60"])
    r.run("trans_keys", 3, {"track": 1, "team": 4, "car": 1, "manual": 1}, ".L....L....R....R....L..S", ["1", "6", "11", "16", "21*", "30"])
    r.run("trans_esc", 3, start, "...A", ["2"])
    open(os.path.join(out, "arcade.txt"), "w", newline="\n").write("\n".join(r.cases) + "\n")
    return len(r.cases), r.nframes
