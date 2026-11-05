"""The tracks' animated scenery against the running original (docs/development.md "Animated scenery").

Usage: python -I tools/s2/dosbox_anim.py <track 1..7> [seconds [shot seconds ...]]       (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame, the data copied,
never linked, the owner's game folder is only read) whose CHOICE.DAT has DETAIL LEVEL HIGH (the original has the animated rows at HIGH only: 0x3106d), three laps
and all seven tracks open, types its way through the menus to an arcade race on the track, hands the player's car to the game's own driver after the start
(tools/dosram.py finds the game's data in DOSBox's process; the one thing written is that car's controls pointer, as dosbox_arcade.py --replay does) and then, for
the given seconds (default 120), records every frame of the game (the sample is taken when the race clock 0x13335c changes): the clock, the player's car, the camera
record's place and angles, the ANIMATIONS rows (0x126d5c, 0x4c bytes each, count at 0x126d58) and the engine's object record of each row that has one (the pointer at
0x11da20 + 0x1c x the row's +0x28). Screenshots (Ctrl+F5, 320 x 200 raw palette indices) are taken at the given seconds after the recording began; their time is a
line of the recording, so that the frame they show can be found. Output in build/check/dosbox_anim_t<n>/: frames.jsonl (never committed), the screenshots,
labels.txt. tools/s2/check_anim_s2.sh reads it.

Only the DOSBox this script started is keyed and read (found by its configuration file); another one running (the owner playing) is never touched. The window must
stay in front for the run (about 3 + the seconds recorded + 1 minutes). The captures go into build/check, not into the game folder.
"""
import json
import math
import os
import struct
import subprocess
import sys
import threading
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402

ORDER = da.ORDER                                # the TRACK page's order of the tracks (menu position -> file number)
CLOCK, ROWS_N, ROWS = 0x13335C, 0x126D58, 0x126D5C
OBJECTS = 0x11DA20                              # the engine's object table: a record pointer at 0x11da20 + 0x1c x the number
CAR = 0x119300                                  # the player's car (0xcc0 bytes a car)
CAMERA = 0x1192F4                               # pointer to the camera record
DETAIL = 0x130C54


def frames(step, work, mem, shots, times):
    """A<label>:<seconds>[:s<label>@<t> ...][:n<row>,<distance>,<gap>,<count>[,<degrees>]] (steps of dosbox_arcade.session): the recording described in the header. n: a
    screenshot whenever the row's object is within `distance` units of the camera and in front of it (within 25 degrees, or the given ones, of its line of sight),
    at least `gap` seconds after the last one, `count` at most"""
    f = step.split(":")
    secs = float(f[1])
    events = sorted((float(e.split("@")[1]), e.split("@")[0]) for e in f[2:] if e[0] == "s")
    near = [tuple(float(v) for v in e[1:].split(",")) for e in f[2:] if e[0] == "n"]
    taken, last_shot = 0, -1e9
    g = mem.g
    da.front(da.keyed_pid)
    out = open(os.path.join(work, "frames.jsonl"), "w")
    t0, last, n = time.time(), None, 0
    detail = struct.unpack("<i", g.flat(DETAIL, 4))[0]
    out.write(json.dumps({"detail": detail, "anim_on": struct.unpack("<i", g.flat(0x124208, 4))[0]}) + "\n")
    while time.time() - t0 < secs:
        now = time.time() - t0
        while events and events[0][0] <= now:
            _, what = events.pop(0)
            if da.foreground_pid() != da.keyed_pid:         # (something else took the front: back to our DOSBox before a key goes anywhere)
                da.front(da.keyed_pid)
            times.append(time.time())
            shots.append(what[1:])
            for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                da.key(sc, up)
                time.sleep(0.02)
            out.write(json.dumps({"shot": what[1:], "t": round(now, 4), "after_frame": n}) + "\n")
        clock = g.flat(CLOCK, 4)
        if clock == last:
            time.sleep(0.003)
            continue
        last = clock
        cnt = struct.unpack("<i", g.flat(ROWS_N, 4))[0]
        rows = g.flat(ROWS, 0x4C * cnt) if 0 < cnt <= 40 else b""
        objs = []
        for i in range(cnt if rows else 0):
            row = rows[0x4C * i:0x4C * (i + 1)]
            ptr = None if row[0] == 0x40 else struct.unpack_from("<I", g.flat(OBJECTS + 0x1C * struct.unpack_from("<i", row, 0x28)[0], 4), 0)[0]
            objs.append((g.ptr(ptr, 0x40) or b"").hex() if ptr else "")
        cam = struct.unpack("<I", g.flat(CAMERA, 4))[0]
        camrec = g.ptr(cam, 0x64) if cam else b""
        out.write(json.dumps({"t": round(now, 4), "clock": clock.hex(), "car": g.flat(CAR + 0x4A0, 0x60).hex(), "cam": camrec.hex(),
                              "view": struct.unpack("<i", g.ptr(cam + 0xAE4, 4))[0] if cam else -1, "rows": rows.hex(), "objs": objs}) + "\n")
        n += 1
        for spec in near:
            row, dist, gap, count = spec[:4]
            half = spec[4] if len(spec) > 4 else 25.0
            o = objs[int(row)] if int(row) < len(objs) else ""
            if o and camrec and taken < count and now - last_shot >= gap:
                ox, oh, oy = struct.unpack_from("<3i", bytes.fromhex(o), 8)
                cx, ch, cy = struct.unpack_from("<3i", camrec, 0x48)
                pitch, yaw = struct.unpack_from("<2f", camrec, 0x54)
                d = (ox - cx, oy - cy, oh - ch)
                dl = math.sqrt(sum(v * v for v in d))
                fwd = (math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch))
                if 0 < dl <= dist and sum(a * b for a, b in zip(d, fwd)) / dl > math.cos(math.radians(half)):
                    events.insert(0, (0.0, "snear%d_%d" % (int(row), taken)))
                    taken += 1
                    last_shot = now
    out.close()
    print("recorded %d frames in %.0f s" % (n, secs))


da.HOOKS["A"] = frames


def voices(step, work, mem, shots, times):
    """V<label>:<seconds>: the game's sound manager (the object at the pointer 0x124274, HMI's digital driver under it) sampled every 100 ms for that long into voices.jsonl: the manager's
    slot table (+0x6164, a handle for each of the 64 slots), the last distance of each slot (+0x6064), the driver's voice records (0xf0 bytes each, 40 of them) and the rows: what
    a slot of the scenery's sounds (7 + row) plays, at which rate, and whether its position moves"""
    secs = float(step.split(":")[1])
    g = mem.g
    out = open(os.path.join(work, "voices.jsonl"), "w")
    t0 = time.time()
    while time.time() - t0 < secs:
        m = struct.unpack("<I", g.flat(0x124274, 4))[0]
        drv = struct.unpack("<I", g.ptr(m + 0x5FDC, 4))[0]
        p = struct.unpack("<I", g.flat(0x115974 + 4 * drv, 4))[0]
        vp = struct.unpack("<I", g.ptr(p + 0x58, 4))[0]
        cnt = struct.unpack("<i", g.flat(ROWS_N, 4))[0]
        out.write(json.dumps({"t": round(time.time() - t0, 3), "clock": g.flat(CLOCK, 4).hex(), "slots": g.ptr(m + 0x6164, 256).hex(), "last": g.ptr(m + 0x6064, 256).hex(),
                              "voices": g.ptr(vp, 40 * 0xF0).hex(), "rows": g.flat(ROWS, 0x4C * cnt).hex() if 0 < cnt <= 40 else ""}) + "\n")
        time.sleep(0.1)
    out.close()


da.HOOKS["V"] = voices


def others():
    """the pids of every DOSBox that is running (tasklist)"""
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    pids = set()
    for line in out.splitlines():
        f = [x.strip('"') for x in line.split('","')]
        if len(f) > 1 and "dosbox" in f[0].lower():
            pids.add(int(f[1]))
    return pids


def watchdog():
    """another DOSBox (the owner's, another agent's) must never meet our keys: if one is running besides the one this script started, the run stops at once and our DOSBox
    goes with it"""
    while True:
        time.sleep(1.0)
        if da.keyed_pid is not None and others() - {da.keyed_pid}:
            print("another DOSBox is running: this run stops, no key is sent to it", flush=True)
            subprocess.run(["taskkill", "/F", "/PID", str(da.keyed_pid)], capture_output=True)
            os._exit(3)


def main():
    if len(sys.argv) < 2 or not sys.argv[1].isdigit() or not 1 <= int(sys.argv[1]) <= 7:
        sys.exit(__doc__)
    if others():
        sys.exit("a DOSBox is running (the owner's, or another agent's): not started, wait until it is gone")
    threading.Thread(target=watchdog, daemon=True).start()
    track = int(sys.argv[1])
    voices_secs = float(sys.argv[sys.argv.index("--voices") + 1]) if "--voices" in sys.argv else 0.0     # --voices <seconds>: the sound manager's voices instead of the rows (the Sound part of the doc)
    secs = float(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2][0].isdigit() else 120.0
    shots = [float(a) for a in sys.argv[3:] if a[0].isdigit() and sys.argv[sys.argv.index(a) - 1] != "--voices"]
    nears = [a for a in sys.argv[3:] if a[0] == "n"]            # n<row>,<distance>,<gap>,<count>: a screenshot whenever the row's object is near and in sight
    da.work = os.path.join(da.root, "build", "check", ("dosbox_voices_t%d" if voices_secs else "dosbox_anim_t%d") % track)
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(4, team=0, car=1, name="MIL", laps=1, detail=2)     # all seven tracks open, three laps, DETAIL HIGH
    pos = ORDER.index(track)
    # the attract sequence plays while the game's memory is looked for; ENTER: TEAM SELECTION, the name, the options room (ARCADE), the title, the TRACK page (RIGHT to the
    # track), the CAR page, the TRANSMISSION page, the loading screen and the race; the car goes to the game's driver after 20 s, then the recording
    # (the TRACK page starts on the track last chosen, which the save of the game folder has: LEFT seven times stops at the first, England, then RIGHT to the track)
    steps = "w3 M k1c w6 k1c w6 k1c w6 k1c w9 k1c w4 " + "ke4b w1.3 " * 7 + "ke4d w1.3 " * pos + "k1c w5 k1c w2 k1c w3 Xarc "
    if voices_secs:
        steps += "Vvoices:%.1f q" % voices_secs
    else:
        steps += "Atrack%d:%.1f:%s q" % (track, secs, ":".join(["sshot%d@%.1f" % (i, t) for i, t in enumerate(shots)] + nears))
    got = da.session(steps)
    print("%d screenshots in %s" % (len(got), da.work))


if __name__ == "__main__":
    main()
