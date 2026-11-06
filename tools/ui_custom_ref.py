"""The set-up pages' references for tools/menu_screens.sh (firmware/s2arcade.c's CAR pages of the championship and of time attack, firmware/s2custom.c): an independent model of what
the original's pages do and draw, written from the game's files and from a reading of STARTL.EXE (docs/screamer2/menus.md row 12; the CAR page 0x3cc2c with its list and choices,
the CAR CUSTOMIZATION page 0x3e1d5, read with tools/dis.sh), not from the firmware. The 3D scenes of both are not modelled: a frame holds only what the 2D toolkit draws over
them (an overlay, 0 where nothing is drawn); the harness compares the pixels the overlay has.

Called by tools/ui_reference.py: build(...) writes into the reference folder
  custom.txt          one line per scripted run: "case <name> <page> <kind> <car> <setup> <script> <result> <calls> <sounds> <setup after> <car after> <free run after> <saved> <frames>"
                      page 0 the CAR page (kind 0 the championship's, 1 time attack's; the car 1 .. 5 and the Custom it opens on, 0 Standard), 1 the CAR CUSTOMIZATION page (kind unused, setup the
                      Custom 1 .. 4 it edits); the script: one character a frame (. none, U D L R A B), the result (1 the race, -1 ESC, 2 CUSTOMIZE taken: the model runs the page and the car
                      page again), the harness's wait_swap_done() calls, the menu sounds in order, the saved six of the car's Custom after the run ("-" none), the frames to compare
  cu_<case>_<tick>    .ov the overlay (palette indices, 0 nothing), .pal the palette as the fade leaves it
The six numbers of the car's four Custom set-ups the harness puts into the save are CUSTOMS below, for team 1 and every car.
"""
import os
import struct

FULL = 0x8000
TITLE, MEDIUM, SMALL, SMALL2 = 0, 1, 2, 3
CUSTOMS = [(3, 1, 2, 4, 0, 3), (0, 4, 1, 3, 3, 0), (2, 2, 0, 2, 2, 2), (4, 4, 2, 0, 1, 2)]        # handling, pressure, tyres, front, rear, brakes
SLOT_OF = (0, 5, 2, 1, 3, 4)                                    # the page's rows HANDLING, BRAKES, TIRE TYPE, TIRE PRESSURE, FRONT, REAR in the six
NAMES = ("HANDLING", "BRAKES", "TIRE TYPE", "TIRE PRESSURE", "FRONT SUSPENSIONS", "REAR SUSPENSIONS", "SAVE", "EXIT")
CURSOR = [(250, 30, 51), (277, 30, 51), (250, 62, 51), (277, 62, 51), (250, 94, 51), (277, 94, 51), (250, 126, 52), (250, 153, 52)]       # 0x113120


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def scaled(pal, k):
    if k >= FULL:
        return [c & 0xFFFFFF for c in pal]
    return [((c >> 16 & 255) * k >> 15) << 16 | ((c >> 8 & 255) * k >> 15) << 8 | (c & 255) * k >> 15 for c in pal]


class Ref:
    def __init__(self, game, graf, spr, fnt, out, helpers):
        self.spr, self.fnt, self.out, self.h = spr, fnt, out, helpers
        bkk, container, datfile = helpers["bkk"], helpers["container"], helpers["datfile"]
        self.pal = bkk(datfile.extract(graf, "BKG_2L.BKK"))[0]
        loc = container(game, "DATA/LOCAL.DAT")
        self.lines = [l for l in datfile.extract(loc, "STRINGS.TXT").decode("latin-1").replace("\r", "").split("\n") if l and not l.startswith("*")]
        self.cases, self.nframes = [], 0

    def at(self, anchor, k=0):
        return self.lines[self.lines.index(anchor) + k]

    def canvas(self):
        return self.h["Canvas"]()

    # ---------------------------------------------------------------------------------------------------------- the CAR page (0x3cc2c): the list and the choice
    def car_page(self, st):
        ref, s = self, self.spr

        class M:
            """state 0 the cars, 1 the list, 2 the choice; st: kind (0 the championship, 1 time attack), car (1 .. 5), setup, free_run, clicks. Every way out fades the page out
            over 13 frames (STARTL.EXE 0x3e056), ESC from the cars too: the result 3 here, the page's result is then -1"""
            fade_out = 13

            def __init__(m, state=0):
                m.state, m.choice, m.t0 = state, 0, 0

            def bonus(m):
                return st["car"] == 5

            def n_choices(m):
                return 2 if st["kind"] == 0 else (3 if st["setup"] != 0 and not m.bonus() else 2)

            def key(m, tick, keys):
                if "A" in keys:
                    if m.state == 0:
                        return 3
                    m.state = 0 if m.bonus() else m.state - 1
                    return 0
                if "B" in keys:
                    st["clicks"].append(6)
                    if m.bonus():
                        if st["kind"] == 0:
                            return 1
                        if m.state == 0:
                            m.state = 2
                            m.choice = min(m.choice, 1)
                            return 0
                    elif m.state == 0:
                        m.state = 1
                        return 0
                    elif m.state == 1:
                        if st["kind"] == 0 and st["setup"] == 0:
                            return 1
                        m.state = 2
                        m.choice = min(m.choice, m.n_choices() - 1)
                        return 0
                    # the choice is taken
                    if st["kind"] == 0:
                        return 2 if m.choice else 1
                    if m.n_choices() == 3 and m.choice == 2:
                        return 2
                    st["free_run"] = 1 if m.choice else 0
                    return 1
                if m.state == 0:
                    d = ("R" in keys) - ("L" in keys)
                    if d:
                        st["car"] = (st["car"] - 1 + d) % st["cars"] + 1
                        st["clicks"].append(11)
                        m.t0 = tick
                elif m.state == 1:
                    if "U" in keys and st["setup"] > 0:
                        st["setup"] -= 1
                    elif "D" in keys and st["setup"] < 4:
                        st["setup"] += 1
                    else:
                        return 0
                    st["clicks"].append(8)
                    m.t0 = tick
                else:
                    if ("L" in keys and m.choice > 0) or ("R" in keys and m.choice < m.n_choices() - 1):
                        m.choice += 1 if "R" in keys else -1
                        st["clicks"].append(8)
                        m.t0 = tick
                return 0

            def draw(m, tick):
                c = ref.canvas()
                clock = (tick - m.t0) * 2
                if m.state and not m.bonus():
                    for i in range(5):
                        lit = i == st["setup"] and ((clock & 0x3F) < 0x20 or m.state == 2)
                        txt = ref.at("Standard", i)
                        w = c.text_width(ref.fnt[SMALL2 if lit else SMALL], 2, txt)
                        c.text(ref.fnt[SMALL2 if lit else SMALL], 3 if lit else 1, 2, (320 - w) >> 1, 88 + 12 * i, txt)
                    if st["setup"]:
                        six = CUSTOMS[st["setup"] - 1]
                        c.sprite(s, 49, 255, 53)
                        for (x, y, v, base) in ((273, 76, six[0], 0x49), (301, 76, six[5], 0x49), (273, 108, six[2], 0x4E), (301, 108, six[1], 0x49), (273, 140, six[3], 0x49),
                                                (301, 140, six[4], 0x49)):
                            c.sprite(s, base + v, x, y)
                if m.state == 2:
                    n = m.n_choices()
                    for k in range(n):
                        lit = not (clock & 0x20) and m.choice == k
                        txt = ref.at("RACE", k) if st["kind"] == 0 else (ref.at("RACE", 1) if k == 2 else ref.at("3 LAPS", k))
                        c.text_boxed(ref.fnt[SMALL2 if lit else SMALL], 3 if lit else 1, 2, (106 if n == 3 else 160) * k, 106 if n == 3 else 160, 155, txt)
                return c
        return M

    # ---------------------------------------------------------------------------------------------------------- CAR CUSTOMIZATION (0x3e1d5)
    def custom_page(self, st):
        ref, s = self, self.spr

        class M:
            fade_out = 13

            def __init__(m):
                m.item, m.open, m.value, m.f, m.minus, m.plus, m.t0 = 0, 0, 0, 0.0, 0.0, 0.0, 0
                m.six = list(st["six"])

            def key(m, tick, keys):
                whole = m.f == 0.0 or m.f == 1.0
                if "A" in keys:
                    if not m.open:
                        m.item = 7
                    else:
                        m.open = 0
                        st["clicks"].append(4)
                elif "B" in keys:
                    if m.item < 6:
                        st["clicks"].append(4)
                        slot = SLOT_OF[m.item]
                        if not m.open:
                            m.open, m.value, m.f = 1, m.six[slot], 0.0
                        else:
                            m.open = 0
                            m.six[slot] = m.value
                    else:
                        if m.item == 6:
                            st["saved"] = tuple(m.six)
                            st["clicks"].append(7)
                        else:
                            st["clicks"].append(13)
                        return 1
                elif "L" in keys and whole:
                    if not m.open:
                        if m.item > 0:
                            m.item -= 1
                            st["clicks"].append(12)
                    else:
                        m.value = max(m.value - 1, 0)
                        st["clicks"].append(0)
                        m.t0, m.minus = tick, 1.0
                elif "R" in keys and whole:
                    if not m.open:
                        m.item += 1
                        if m.item >= 8:
                            m.item = 7
                        else:
                            st["clicks"].append(12)
                    else:
                        top = 2 if m.item == 2 else 4
                        if m.value < top:
                            m.value += 1
                            st["clicks"].append(0)
                        m.plus, m.t0 = 1.0, tick
                elif "U" in keys and whole and not m.open:
                    if m.item > 5:
                        m.item -= 1
                    elif m.item > 1:
                        m.item -= 2
                elif "D" in keys and whole and not m.open:
                    if m.item < 5:
                        m.item += 2
                    elif m.item < 7:
                        m.item += 1
                return 0

            def draw(m, tick):
                c = ref.canvas()
                c.sprite(s, 49, 250, 30)
                c.sprite(s, 50, 250, 128)
                title = ref.at("CAR CUSTOMIZATION")
                c.text(ref.fnt[TITLE], 1, 2, (320 - c.text_width(ref.fnt[TITLE], 2, title)) >> 1, 0, title)
                x, y, spr = CURSOR[m.item]
                c.sprite(s, spr, x, y)
                m.f = f32(m.f + f32(f32(0.02) * 8.0)) if m.open else f32(m.f - f32(f32(0.02) * 8.0))
                m.f = 1.0 if m.f > 1.0 else 0.0 if m.f < 0.0 else m.f
                f = m.f
                on = ((tick - m.t0) * 2 & 0x3F) < 0x20
                if m.item != 2:
                    c.sprite(s, 0x3D + min(m.item, 5), int(87.0 * f - 45.0), 90)
                    c.sprite(s, 0x35, 17, int(200.0 - 50.0 * f))
                    c.sprite(s, 0x3C if m.minus else 0x48, 5, int(210.0 - 50.0 * f))
                    c.sprite(s, 0x3B if m.plus else 0x47, 104, int(206.0 - 50.0 * f))
                    if on:
                        c.sprite(s, 0x36 + m.value, 17 + 16 * m.value, int(200.0 - 50.0 * f))
                else:
                    c.sprite(s, 0x3D + 2, int(87.0 * f - 45.0), 90)
                    for k, x in enumerate((8, 60, 112)):
                        c.sprite(s, 0x43 + k, x, int(200.0 - 50.0 * f))
                    c.text(ref.fnt[SMALL], 1, 2, 170, int(200.0 - 42.0 * f), ref.at("A - Dry", m.value))
                    if on:
                        c.sprite(s, 0x46, 52 * m.value + 8, int(200.0 - 50.0 * f))
                name = ref.at("HANDLING", m.item)
                c.text(ref.fnt[MEDIUM], 1, 2, (320 - c.text_width(ref.fnt[MEDIUM], 2, name)) >> 1, 180, name)
                m.minus = max(f32(m.minus - f32(f32(0.02) * 15.0)), 0.0)
                m.plus = max(f32(m.plus - f32(f32(0.02) * 15.0)), 0.0)
                return c
        return M

    # ---------------------------------------------------------------------------------------------------------- the runner
    def run(self, name, page, kind, car, setup, script, frames):
        """a scripted run: the CAR page (page 0), which runs the CAR CUSTOMIZATION page when CUSTOMIZE is taken and comes back to the list (state 1, the clock started again);
        or the CAR CUSTOMIZATION page alone (page 1). The frames are counted over the pages together, as the harness counts them."""
        st = dict(kind=kind, car=car, setup=setup, free_run=0, clicks=[], cars=5, saved=None, six=CUSTOMS[setup - 1] if page == 1 else None)
        want = {int(f) for f in frames}
        trace = {}

        def run_model(make, start):
            """one page from global frame `start`: its result and the first global frame after it; the wanted frames' overlays go into trace"""
            model = make()
            res, fade, local = 0, FULL, 0
            step = (FULL + model.fade_out - 1) // model.fade_out
            while True:
                g = start + local
                ch = script[g] if g < len(script) else "."
                prev = script[g - 1] if 0 < g <= len(script) else "."
                keys = {ch} if ch != "." and ch != prev else set()
                if not res:
                    res = model.key(local, keys)
                    if res < 0:
                        return res, g + 1                           # a cut: the page returns before it draws this frame
                c = model.draw(local)
                if g in want:
                    trace[g] = (bytes(c.px), fade)
                if res:
                    fade -= step
                    if fade <= 0:
                        return res, g + 1
                local += 1

        if page == 1:
            result, end = run_model(self.custom_page(st), 0)
        else:
            Car = self.car_page(st)
            result, end = run_model(lambda: Car(0), 0)
            while result == 2:
                st["six"] = CUSTOMS[st["setup"] - 1]
                _, end = run_model(self.custom_page(st), end)
                result, end = run_model(lambda: Car(1), end)
            result = -1 if result == 3 else result
        frames = [f for f in frames if int(f) <= end - 2]                   # (a frame after the page's end is not seen: the lists were made for longer fades)
        assert len(frames) >= 2, f"{name}: no frame is seen before the end ({end} calls)"
        for g in (int(f) for f in frames):
            assert g in trace, f"{name}: frame {g} was not drawn"
            px, fade = trace[g]
            base = os.path.join(self.out, f"cu_{name}_{g}")
            open(base + ".ov", "wb").write(px)
            open(base + ".pal", "wb").write(b"".join(struct.pack("<I", c) for c in scaled(self.pal, fade)))
            self.nframes += 1
        clicks = ",".join(str(k) for k in st["clicks"]) or "-"
        saved = "-" if not st["saved"] else ",".join(str(v) for v in st["saved"])
        self.cases.append(f"case {name} {page} {kind} {car} {setup} {script} {result} {end} {clicks} {st['setup']} {st['car']} {st['free_run']} {saved} {','.join(frames) or '-'}")


def S(spec):
    """a script from "<frame>:<key> ...": the key's character in that frame, a dot in the others"""
    keys = [(int(a), b) for a, b in (w.split(":") for w in spec.split())]
    out = ["."] * (max(t for t, _ in keys) + 1)
    for t, k in keys:
        out[t] = k
    return "".join(out)


def build(game, graf, spr, fnt, out, helpers):
    r = Ref(game, graf, spr, fnt, out, helpers)
    # --- the CAR page of time attack: the set-up list (the first start, Standard lit and blinking), DOWN to Custom 1 (the panel), ENTER: the choice in thirds, RIGHT RIGHT: CUSTOMIZE lit,
    # ESC back to the list and the cars and out
    r.run("ta_list", 0, 1, 2, 0, S("10:B 30:D 60:B 90:R 130:R 170:A 200:A 230:A"), ["12", "28", "31", "45", "47", "62", "65", "92", "100", "131", "150", "172", "176", "201"])
    r.run("ta_std", 0, 1, 2, 0, S("10:B 30:B 60:R 90:B"), ["12", "28", "35", "45", "62", "80", "92"])
    r.run("ta_free", 0, 1, 4, 3, S("10:B 30:B 60:R 90:B"), ["15", "35", "50", "70", "92"])
    r.run("ta_bonus", 0, 1, 5, 2, S("10:B 30:R 60:R 90:A 120:A"), ["12", "35", "70", "92"])
    r.run("ta_bounds", 0, 1, 1, 0, S("10:B 20:U 30:D 40:D 50:D 60:D 70:D 80:A 100:A"), ["15", "25", "45", "65", "75", "85"])
    # --- the championship's: Standard races at once; a Custom asks RACE / CUSTOMIZE; the bonus car races at once; CUSTOMIZE runs the page and comes back to the list; the cars turn
    r.run("lg_std", 0, 0, 3, 0, S("10:B 30:B"), ["12", "28", "35"])
    r.run("lg_race", 0, 0, 3, 2, S("10:B 30:B 60:B"), ["12", "35", "62", "80"])
    r.run("lg_custom", 0, 0, 2, 1, S("10:B 30:B 60:R 90:B 150:A 170:B 220:A 250:A"), ["12", "35", "62", "95", "130", "160", "175", "200", "230"])
    r.run("lg_bonus", 0, 0, 5, 3, S("10:B"), ["5", "8"])
    r.run("lg_cars", 0, 0, 1, 1, S("10:R 70:R 130:L 190:A"), ["15", "75", "135", "185"])
    # --- the CAR CUSTOMIZATION page: the walk over the items (every key in every place), a picture opened and a value changed (the slide, the flashes, the blink clock started again, a
    # key while the picture slides is not taken), the tyres' page, a value kept, one dropped by ESC, ESC with nothing open (the cursor to EXIT), SAVE writes the six, EXIT does not
    r.run("cu_walk", 1, 0, 2, 1, S("6:R 12:R 18:R 24:R 30:R 36:R 42:R 48:R 54:R 60:L 66:D 72:D 78:U 84:U 90:U 96:U 102:L 108:D 114:D 120:D 126:D 132:U 138:U 144:U 150:A 156:B"),
          ["3", "7", "13", "19", "25", "31", "37", "43", "49", "55", "61", "67", "73", "79", "85", "91", "97", "103", "109", "115", "121", "127", "133", "139", "145", "151", "157", "160"])
    r.run("cu_edit", 1, 0, 2, 1, S("5:B 8:R 14:R 20:R 26:L 32:L 40:B 52:D 58:B 64:R 70:L 76:L 82:L 88:R 94:A 110:D 116:B 124:R 130:L 136:L 142:B 160:D 166:B"),
          ["6", "9", "12", "15", "21", "27", "33", "41", "43", "48", "60", "62", "68", "71", "77", "83", "89", "95", "100", "118", "120", "126", "131", "137", "143", "150", "170"])
    r.run("cu_tyres", 1, 0, 2, 3, S("5:B 10:R 30:R 36:R 50:L 70:L 90:L 110:B 120:B 126:A 130:B 140:A 150:D 160:D 166:D 172:D 178:B"), ["6", "7", "8", "9", "12", "34", "55", "75", "95", "112", "118", "150"])
    r.run("cu_keep_esc", 1, 0, 2, 4, S("5:B 14:R 22:R 30:A 50:B 60:R 70:A 80:A 90:D 100:D 110:D 120:D 130:B"), ["6", "7", "10", "14", "16", "32", "52", "62", "72", "82", "132", "140"])
    r.run("cu_exit", 1, 0, 2, 3, S("5:B 14:R 22:R 30:B 40:A 50:B"), ["6", "10", "20", "32", "45", "55"])
    open(os.path.join(out, "custom.txt"), "w", newline="\n").write("\n".join(r.cases) + "\n")
    return len(r.cases), r.nframes
