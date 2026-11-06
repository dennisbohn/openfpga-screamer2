"""The team room scenes of the CAR page and the CAR CUSTOMIZATION page against the running original (docs/screamer2/menus.md "The garage").

Usage: python -I tools/s2/dosbox_garage.py [car|cars|custom|all]          (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

Starts STARTL.EXE in the game folder's own DOSBox (the one this script starts, found by its configuration file; no other DOSBox is ever keyed or read) on a private copy of the
game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame inside the work tree, copied, never linked; team 1 CONDOR named MIL, its second car chosen, every team's
fifth car won, Custom 1 and Custom 2 of that car set), types its way through TEAM SELECTION, the name and the OPTIONS ROOM to TIME ATTACK's TRACK SELECTION and from there into
the CAR page (STARTL.EXE 0x3cc2c) and, for `custom` / `all`, on into the CAR CUSTOMIZATION page (0x3e1d5), and records for the whole time, as often as the process allows,
everything the two pages' pictures depend on: the camera record (0x131ea4: the angles a0 a1 a2 in 2048ths, then the place x, y up, z), the pages' words (0x132494 ..: the next
page, the leaving flag, the fade, the done flag, the sway 0x1324a4 and the look-at angles 0x1324a8 / 0x1324ac), the frame time, the hundredths, the engine's animation clock, the
set-up and state words, and the object records of the five cars and their five shadows (the table 0x130fe0 / 0x131030, 8 bytes each; the record pointer at 0x11da20 + 0x1c x the
number: +8 x, +0xc y up, +0x10 z, +0x14 .. +0x1c the angles, 0x40 bytes read). Every change is a line of build/check/dosbox_garage/frames.jsonl (never committed); the keys and the raw
screenshots (Ctrl+F5, 320 x 200 palette indices) are lines of it too, with their time, so that the frame a screenshot shows can be found. tools/host/garage_match.c
(tools/garage.sh) reads both.

The window of the DOSBox must stay in front for the run (about 4 minutes); the guard of tools/s2/dosbox_arcade.py refuses to send a key to anything else. The captures go into
build/check, not into the game folder.
"""
import json
import os
import struct
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402

CAM = 0x131EA4                                  # the camera record: a0 a1 a2, x y z
PAGE = 0x132494                                 # next page, leaving, fade, done, sway, look-at pitch, look-at heading (7 words)
AIM = 0x1324B4                                  # the custom page's point looked at
WORDS = [(0x13237C, 4), (0x1324C4, 4), (0x1324D4, 4), (0x1324E8, 4), (0x1324E4, 4), (0x1324DC, 4), (0x133350, 4), (0x4CFD6, 4), (0x1347A0, 4), (0x128A9C, 8)]
CAR_TABLE, SHADOW_TABLE, OBJECTS = 0x130FE0, 0x131030, 0x11DA20
MCM = (0x131F34, 3 * 0x16C)                     # P_<t>.MCM as the game keeps it: three records of 0x16c bytes (count, 15 places, 15 aims)


def records(g, table):
    """the five object records' pointers of a table (the cars or their shadows)"""
    out = []
    for i in range(5):
        n = struct.unpack("<i", g.flat(table + 8 * i, 4))[0]
        out.append(struct.unpack("<I", g.flat(OBJECTS + 0x1C * n, 4))[0] if 0 <= n < 200 else 0)
    return out


def record(step, work, mem, shots, times):
    """G<seconds>[:<event>@<t> ...] (a step of dosbox_arcade.session): the recording described in the header; events: k<hex> / ke<hex> a key, s<label> a screenshot"""
    f = step.split(":")
    secs = float(f[0])
    events = sorted((float(e.split("@")[1]), e.split("@")[0]) for e in f[1:])
    g = mem.g
    da.front(da.keyed_pid)
    out = open(os.path.join(work, "frames.jsonl"), "w")
    out.write(json.dumps({"mcm": g.flat(*MCM).hex()}) + "\n")
    t0, last, ptrs, checked = time.time(), None, ([0] * 5, [0] * 5), -1.0
    while time.time() - t0 < secs:
        now = time.time() - t0
        while events and events[0][0] <= now:
            _, what = events.pop(0)
            if da.foreground_pid() != da.keyed_pid:
                da.front(da.keyed_pid)
            try:
                if what[0] == "s":
                    for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                        da.key(sc, up)
                        time.sleep(0.02)
                    times.append(time.time())
                    shots.append(what[1:])
                    out.write(json.dumps({"shot": what[1:], "t": round(time.time() - t0, 4)}) + "\n")
                else:
                    ext = what[1] == "e"
                    sc = int(what[2:] if ext else what[1:], 16)
                    da.key(sc, 0, ext)
                    time.sleep(0.03)
                    da.key(sc, 1, ext)
                    out.write(json.dumps({"key": what, "t": round(time.time() - t0, 4)}) + "\n")
            except SystemExit as e:             # (the guard of dosbox_arcade.key: the window in front is not ours and the key was not sent: the recording goes on and says so)
                out.write(json.dumps({"refused": what, "t": round(time.time() - t0, 4)}) + "\n")
                print("refused:", what, e)
        if now - checked > 0.5:                 # (the objects of a page come and go: the tables are read again from time to time)
            ptrs, checked = (records(g, CAR_TABLE), records(g, SHADOW_TABLE)), now
        cur = (g.flat(CAM, 24), g.flat(PAGE, 28), g.flat(AIM, 12), [g.flat(a, n) for a, n in WORDS], [(g.ptr(p, 0x40) or b"") if p else b"" for p in ptrs[0] + ptrs[1]])
        if cur != last:
            out.write(json.dumps({"t": round(time.time() - t0, 4), "cam": cur[0].hex(), "page": cur[1].hex(), "aim": cur[2].hex(), "words": [w.hex() for w in cur[3]],
                                  "objs": [o.hex() for o in cur[4]]}) + "\n")
            last = cur
    out.close()
    print("recorded %.0f s" % (time.time() - t0))


da.HOOKS["G"] = record


def script(which):
    """the keys and shots of the recording (seconds from its start, which is the ENTER on TRACK SELECTION); the original's keys: ENTER 0x1c, ESC 0x01, up 0x48, left 0x4b, right 0x4d, down 0x50.
    As few events as will do (each one needs the window in front: a key the guard refuses is not sent): `cars` the cars changed round all five with the bonus car's page, `car` is the CAR page alone (the camera's orbit was recorded for 20 s in a
    first run: the model matches 934 of 990 rows, the rest are torn reads), `custom` goes through it to the CAR CUSTOMIZATION page, `all` both."""
    ev = [("k1c", 0.3)]                          # TRACK SELECTION -> the CAR page (the room, the cars: it loads for a moment)
    t = 5.0
    if which == "cars":                          # the cars changed (the swing starts again at 0.125), round the five and past the end, the bonus car's page, ESC
        ev.append(("sstart", t))
        t += 1.5
        for i, (d, name) in enumerate((("ke4d", "r1"), ("ke4d", "r2"), ("ke4d", "r3"), ("ke4d", "r4"), ("ke4d", "wrap"), ("ke4b", "l_wrap"), ("ke4b", "l4"))):
            ev.append((d, t))
            for j, dt in enumerate((0.1, 0.5, 1.2) if i in (0, 4, 5) else (0.6,)):
                ev.append(("s%s_%d" % (name, j), t + dt))
            t += 2.0
        ev += [("k1c", t), ("sbonus_enter0", t + 0.4), ("sbonus_enter1", t + 1.2)]       # the bonus car (the fifth): time attack goes to the choice at once
        t += 2.0
        ev += [("ke4d", t), ("sbonus_free", t + 0.5), ("k01", t + 1.0), ("sbonus_esc", t + 1.5), ("k01", t + 2.5), ("sleave0", t + 2.52), ("sleave1", t + 2.6), ("sleave2", t + 2.7)]
        return ev, t + 5
    many = which != "custom"
    if many:
        for i in range(3):                       # the page settled, the camera going round the chosen car
            ev.append(("sorbit%02d" % i, t))
            t += 2.0
        t += 1.0
        for d, name in (("ke4d", "right"), ("ke4b", "left")):      # the next car, the first one again (wrap round is the last of five: not tried here)
            ev.append((d, t))
            for j, dt in enumerate((0.15, 0.5, 1.3, 3.0)):
                ev.append(("s%s_%d" % (name, j), t + dt))
            t += 5.0
    else:
        ev.append(("sorbit00", t))
        t += 2.0
    ev.append(("k1c", t))                        # the list
    for j, dt in enumerate((0.15, 0.5, 1.3) if many else (0.5,)):
        ev.append(("slist0_%d" % j, t + dt))
    t += 2.5 if many else 1.2
    for n in (1, 2) if many else (1,):
        ev.append(("ke50", t))                   # Custom 1, Custom 2
        ev.append(("slist%d_0" % n, t + 0.8))
        t += 1.6
    ev.append(("k1c", t))                        # the choice: 3 LAPS | FREE RUN | CUSTOMIZE
    for j, dt in enumerate((0.15, 0.5, 1.3) if many else (0.5,)):
        ev.append(("schoice0_%d" % j, t + dt))
    t += 2.0 if many else 1.2
    ev += [("ke4d", t), ("ke4d", t + 0.6), ("schoice2_0", t + 1.4)]
    t += 2.2
    if which == "car":
        ev += [("k01", t), ("slist_back", t + 0.8), ("k01", t + 1.6), ("scars_back", t + 2.4)]
        return ev, t + 5
    ev.append(("k1c", t))                        # CUSTOMIZE: the page fades out (13 frames), the other page comes up and the camera flies in from where it starts
    for j, dt in enumerate((0.1, 0.4, 0.9, 1.6, 2.5, 3.5, 5.0)):
        ev.append(("scu_fly%d" % j, t + dt))
    t += 7.0
    for item in range(7):                        # the eight items: the camera to each station (one shot in flight, one settled)
        ev.append(("ke4d", t))
        ev.append(("sitem%d_a" % (item + 1), t + 0.5))
        ev.append(("sitem%d_b" % (item + 1), t + 3.2))
        t += 4.0
    ev += [("k01", t), ("k1c", t + 1.0)]         # ESC puts the cursor on EXIT, ENTER leaves (the fade)
    for j, dt in enumerate((1.9, 3.0, 5.0)):
        ev.append(("sback%d" % j, t + dt))
    return ev, t + 8


def main():
    which = next((a for a in sys.argv[1:] if a in ("car", "cars", "custom", "all")), "custom")
    da.work = os.path.join(da.root, "build", "check", "dosbox_garage")
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(4, team=0, car=1, name="MIL", cars5=True, setups={(0, 1, 0): (3, 1, 2, 4, 0, 3), (0, 1, 1): (0, 4, 1, 3, 3, 0)})
    ev, secs = script(which)
    boot = "w10 M k1c w6 k1c w6 k1c w6 ke4d w4 ke4d w4 k1c w7 "      # the options room's third station, TIME ATTACK: TRACK SELECTION
    steps = boot + "G%g:%s q" % (secs, ":".join("%s@%.2f" % (k, t) for k, t in ev))
    shots = da.session(steps)
    print("%d screenshots in %s" % (len(shots), da.work))


if __name__ == "__main__":
    main()
