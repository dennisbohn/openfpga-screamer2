"""The attract sequence against the running original (docs/screamer2/menus.md "The attract sequence", docs/development.md "The attract sequence").

Usage: python -I tools/s2/dosbox_attract.py [--press <stage>] [--seconds <n>]       (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")
       python -I tools/s2/dosbox_attract.py --check [<dir>]                         (the last run's recording again, without DOSBox)

The game starts with the attract page (STARTL.EXE 0x3ca20): the race of four computer cars, the logos and the credits (0x37ec1). The script starts STARTL.EXE in the game folder's own DOSBox on
a private copy of the game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame, the data copied, never linked, the owner's folder is only read; its CHOICE.DAT has the attract's track
counter at 0 and DETAIL LEVEL HIGH), finds the game's data in DOSBox's process (tools/dosram.py) and records the game's own variables every time the race clock 0x13335c changes, until the
sequence is over (the race is over: 0x11f0a8 & 3) or the seconds are up (default 150):
    the race clock, the step 0x133350 and the raw time 0x133354, the easing table's value for the logos (0x1240c0, row 0x55), the screen's variables (stage 0x127ecc, 0x127ed0, 0x127ed4, the logo's place 0x128888, the credits' line 0x128890, the ending flag
    0x132498 and the way to black 0x13249c, the song's cue 0x11f09c, the race flags 0x11f0a8), the camera record (0x1192f4: the camera in use +0x24, the car followed +0xadc, the ten-second flag
    +0xae8, the place +0x48 and angles +0x54), and each of the four cars' record (flags +0x4c, laps +0x4a8, place +0xc30, pace +0x48, node +0x4b8, way +0x4bc, place +0x4d8 / +0x4dc / +0x4e0,
    speed +0x500);
and, once at the start, the race's record (mode, track, level, cars, the cars' teams and models, laps), the table of ATTRACT.TXT's lines and the logos' sprite records. Screenshots (Ctrl+F5, 320 x 200
raw palette indices) are taken when the game's own variables say so (the logo sliding and standing, the white, a group of the credits, a camera, the black, the last frame), and their time is a
row of the recording. --press <stage>: ENTER is pressed once that stage has shown for a second (1 logo, 2 logo, 3 white, 4 credits): how the sequence ends on a key.
Output in build/check/dosbox_attract/ (never committed): rows.jsonl, header.json, the screenshots and labels.txt. tools/attract_compare.py reads it.

Only the DOSBox this script started is keyed and read (found by its configuration file); another one running (the owner playing, another agent's) stops the run at once, and the run does not start
while one is running. The window must stay in front for the run (about 3 minutes). The captures go into build/check, not into the game folder.
"""
import json
import os
import struct
import subprocess
import sys
import threading
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402

CLOCK, STEP, RAW = 0x13335C, 0x133350, 0x133354
CAMERA = 0x1192F4
CAR = 0x119300


def f(b, o):
    return struct.unpack_from("<f", b, o)[0]


def i(b, o):
    return struct.unpack_from("<i", b, o)[0]


def sample(g):
    """one row of the game's variables"""
    a = g.flat(0x127EC0, 0x30)
    x = g.flat(0x128888, 0x10)
    p = g.flat(0x132494, 0x0C)
    t = g.flat(0x133350, 0x10)
    s = g.flat(0x11F098, 0x14)
    cam_ptr = struct.unpack("<I", g.flat(CAMERA, 4))[0]
    cam = (g.ptr(cam_ptr, 0xAEC) if cam_ptr else b"") or b""
    if len(cam) < 0xAEC:
        cam = b""
    car_ptr = struct.unpack_from("<I", cam, 0xADC)[0] if cam else 0
    car_at = g.to_flat(car_ptr) if car_ptr else None
    follow = (car_at - CAR) // 0xCC0 if car_at is not None else -1
    ep = struct.unpack("<I", g.flat(0x1240C0, 4))[0]            # the table of the easing (0x315a4) in use: row 0x55 is the logos' (0x111d6)
    kb = g.ptr(ep + 0x55 * 4, 4) if ep else None                 # (the tables are on the game's heap: a guest pointer)
    row = {"k": f(kb, 0) if kb and len(kb) == 4 else 0.0,
           "step": f(t, 0), "raw": f(t, 4), "clock": f(t, 0xC), "stage": i(a, 0xC), "U": f(a, 0x10), "T": f(a, 0x14), "X": f(x, 0), "group": i(x, 8),
           "page": i(p, 0), "L": f(p, 4), "B": f(p, 8), "cued": i(s, 4), "flags": i(s, 0x10)}
    if cam:
        row.update({"shot": i(cam, 0x24), "last": i(cam, 0x28), "follow": follow, "rot": i(cam, 0xAE8), "cam_pos": list(struct.unpack_from("<3i", cam, 0x48)),
                    "cam_ang": list(struct.unpack_from("<3f", cam, 0x54)), "view": i(cam, 0xAE4)})
    cars = []
    for k in range(4):
        c = g.flat(CAR + 0xCC0 * k + 0x40, 0x4D0)
        cars.append([i(c, 0x4C - 0x40), i(c, 0x4A8 - 0x40), i(c, 0xC30 - 0x40) if 0xC30 - 0x40 < len(c) else -1, f(c, 0x48 - 0x40), f(c, 0x4B8 - 0x40), f(c, 0x4BC - 0x40),
                     i(c, 0x4D8 - 0x40), i(c, 0x4DC - 0x40), i(c, 0x4E0 - 0x40), f(c, 0x500 - 0x40)])      # (the place: whole numbers x, height, z)
    row["cars"] = cars
    # place and laps past the first block: 0xc30 is beyond 0x40 + 0x4d0, so read it apart
    for k in range(4):
        cars[k][2] = i(g.flat(CAR + 0xCC0 * k + 0xC30, 4), 0)
    return row


def header(g):
    """the race's record, ATTRACT.TXT's lines as the game holds them, the logos' sprite records"""
    h = {"mode": i(g.flat(0x130D10, 4), 0), "cars": i(g.flat(0x130C48, 4), 0), "track": i(g.flat(0x130C4C, 4), 0), "condition": i(g.flat(0x130C50, 4), 0),
         "detail": i(g.flat(0x130C54, 4), 0), "laps": i(g.flat(0x130D48, 4), 0), "level": i(g.flat(0x130D4C, 4), 0), "music": i(g.flat(0x130D64, 4), 0),
         "damages": i(g.flat(0x130D54, 4), 0), "counter": i(g.flat(0x128A58, 4), 0)}
    h["entries"] = [list(struct.unpack("<11i", g.flat(0x130C58 + 0x2C * k, 0x2C))) for k in range(4)]
    lines = []
    for k in range(160):
        p = struct.unpack("<I", g.flat(0x1286A8 + 4 * k, 4))[0]
        if not p:
            break
        s = (g.ptr(p, 40) or b"").split(bytes(1))[0].decode("latin-1").rstrip(chr(13))
        lines.append(s)
        if k > 3 and s == "#" and lines[-2] == "#":
            break
    h["lines"] = lines
    table = struct.unpack("<I", g.flat(0x124210, 4))[0]            # (the sprite table: on the heap)
    h["sprites"] = {n: list(struct.unpack("<5i", g.ptr(table + 0x14 * n, 0x14))) for n in (0x6E, 0x6F, 0x70, 0x71)}
    return h


def watch(step, work, mem, shots, times):
    """N<label>:<seconds>[:press<stage>]: the recording described in the header"""
    fld = step.split(":")
    secs = float(fld[1])
    press = int(fld[2][5:]) if len(fld) > 2 and fld[2].startswith("press") else 0
    g = mem.g
    da.front(da.keyed_pid)
    hdr = None                                  # read once the screen draws its first sprite (the race has loaded its sprites and the text, 0x3ca3f)
    want_groups = {2}
    fired = set()

    def in_front():
        """our DOSBox in front (a few tries: the owner is on this PC); False if it will not come, and then no key is sent"""
        for _ in range(6):
            if da.foreground_pid() == da.keyed_pid:
                return True
            da.front(da.keyed_pid)
        return da.foreground_pid() == da.keyed_pid

    def shot(label, now):
        if not in_front():
            out.write(json.dumps({"shot_missed": label, "t": round(now, 4)}) + "\n")
            return
        times.append(time.time())
        shots.append(label)
        try:
            for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                da.key(sc, up)
                time.sleep(0.02)
        except SystemExit:                      # (the window went away between two keys: the rest of the chord is not sent)
            out.write(json.dumps({"shot_missed": label, "t": round(now, 4)}) + "\n")
            return
        out.write(json.dumps({"shot": label, "t": round(now, 4), "after_row": n}) + "\n")

    out = open(os.path.join(work, "rows.jsonl"), "w")
    t0, last, n, pressed, cam_next, over_seen = time.time(), None, 0, False, 20.0, None
    prev_stage, stage_t0 = -1, 0.0
    while time.time() - t0 < secs:
        now = time.time() - t0
        row = sample(g)
        key = (row["clock"], row["stage"], row["U"], row["X"], row["B"])
        if key == last:
            time.sleep(0.002)
            continue
        last = key
        row["t"] = round(now, 4)
        out.write(json.dumps(row) + "\n")
        n += 1
        st, U, X, clock, grp = row["stage"], row["U"], row["X"], row["clock"], row["group"]
        if hdr is None and st >= 1:
            hdr = header(g)
            json.dump(hdr, open(os.path.join(work, "header.json"), "w"))
            print("the game's record: mode %#x, %d cars, track %d, level %d, laps %d, counter %d; %d lines of text" % (hdr["mode"], hdr["cars"], hdr["track"], hdr["level"], hdr["laps"],
                                                                                                      hdr["counter"], len(hdr["lines"])))
            starts = [2]                        # the credits' groups: the lines a group begins at (after a '#'), to take a picture of a few
            for k, s in enumerate(hdr["lines"]):
                if s.startswith("#") and k + 1 < len(hdr["lines"]):
                    starts.append(k + 1)
            want_groups = {2, starts[3], starts[9], starts[len(starts) - 3]} if len(starts) > 10 else {2}
        todo = []
        if st == 1 and 140 < X < 175:
            todo.append("logo1_slide")
        if st == 1 and U > 1.2:
            todo.append("logo1_set")
        if st == 2 and 100 < X < 150:
            todo.append("logo2_slide")
        if st == 2 and U > 1.2:
            todo.append("logo2_set")
        if st == 3 and 0.62 < U < 0.8:
            todo.append("white_a")
        if st == 3 and 0.2 < U < 0.4:
            todo.append("white_b")
        if st == 4 and row["T"] == 0.0 and 0.45 < U < 0.6 and grp in want_groups:
            todo.append("credits_%d" % grp)
        if st >= 4 and clock >= cam_next:
            todo.append("cam_%d" % int(clock))
        if st >= 4 and row["L"] != 0 and 0.3 < row["B"] < 0.7:
            todo.append("fadeout")
        if st >= 4 and row["B"] >= 1.0 and not (row["flags"] & 3):
            todo.append("exit")                 # the frame the way to black ends in: the screen cleared, the S2 logo (it stays up while the next page loads)
        if st < 3 and row["L"] != 0 and not (row["flags"] & 3):
            todo.append("exit_early")
        for lab in todo:
            if lab in fired:
                continue
            fired.add(lab)
            if lab.startswith("cam_"):
                cam_next = clock + 9.0
            shot(lab, now)
            break
        if st != prev_stage:
            stage_t0 = now
        if press and not pressed and st == press and now - stage_t0 > {1: 2.5, 2: 2.5, 3: 0.4, 4: 6.0}.get(press, 1.0):
            if in_front():
                pressed = True
                da.press(0x1C)
                out.write(json.dumps({"key": "ENTER", "t": round(now, 4), "after_row": n, "stage": st}) + "\n")
        if over_seen is None and row["flags"] & 3:      # the race is over (0x11f0a8 |= 3): a moment more for the last frame's screenshot, then the recording ends
            over_seen = time.time()
        if over_seen and time.time() - over_seen > 1.5:
            break
        if prev_stage != st:
            out.write(json.dumps({"stage_change": [prev_stage, st], "t": round(now, 4), "clock": clock}) + "\n")
            prev_stage = st
    out.close()
    print("recorded %d rows in %.0f s" % (n, time.time() - t0))


da.HOOKS["N"] = watch


def others():
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    pids = set()
    for line in out.splitlines():
        fl = [x.strip('"') for x in line.split('","')]
        if len(fl) > 1 and "dosbox" in fl[0].lower():
            pids.add(int(fl[1]))
    return pids


def watchdog():
    """another DOSBox (the owner's, another agent's) must never meet our keys: if one is running besides the one this script started, the run stops at once and our DOSBox goes with it"""
    while True:
        time.sleep(1.0)
        if da.keyed_pid is not None and others() - {da.keyed_pid}:
            print("another DOSBox is running: this run stops, no key is sent to it", flush=True)
            subprocess.run(["taskkill", "/F", "/PID", str(da.keyed_pid)], capture_output=True)
            os._exit(3)


def patch_counter(private, value):
    """CHOICE.DAT of the private copy: the attract's track counter (word 27, +0x6c) set"""
    path = os.path.join(private, "CONFIG", "CHOICE.DAT")
    d = open(path, "rb").read()
    w = list(struct.unpack("<%dI" % (len(d) // 4), d))
    dec = [((v ^ k) - k * 0x1050C) & 0xFFFFFFFF for k, v in enumerate(w)]
    dec[27] = value
    out = [((v + k * 0x1050C) ^ k) & 0xFFFFFFFF for k, v in enumerate(dec)]
    open(path, "wb").write(struct.pack("<%dI" % len(out), *out) + d[len(out) * 4:])


def main():
    if "--check" in sys.argv:
        sys.exit("tools/attract_compare.py reads the recording: python -I tools/attract_compare.py")
    if others():
        sys.exit("a DOSBox is running (the owner's, or another agent's): not started, wait until it is gone")
    threading.Thread(target=watchdog, daemon=True).start()
    press = int(sys.argv[sys.argv.index("--press") + 1]) if "--press" in sys.argv else 0
    secs = float(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 150.0
    da.work = os.path.join(da.root, "build", "check", "dosbox_attract" + ("_press%d" % press if press else ""))
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(4, team=0, car=1, name="MIL", detail=2)
    patch_counter(da.private, 0)
    steps = "w1.2 M Nrun:%.1f%s q" % (secs, ":press%d" % press if press else "")
    for attempt in range(3):                    # (the game may not be in memory yet when the first look is made: a slow machine; a new DOSBox then)
        got = da.session(steps)
        if os.path.exists(os.path.join(da.work, "rows.jsonl")):
            break
        print("the game was not found: trying again (%d)" % (attempt + 1))
    print("%d screenshots in %s" % (len(got), da.work))


if __name__ == "__main__":
    main()
