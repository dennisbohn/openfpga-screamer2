"""The ARCADE title's cars against the running original (docs/development.md "The arcade scene").

Usage: python -I tools/s2/dosbox_scene.py [seconds]           (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame, the data copied, never
linked, the owner's game folder is only read; team 1 named MIL), types its way through TEAM SELECTION, the name and the OPTIONS ROOM to the ARCADE title (0x3b6e8) and
then, for the given seconds (default 45), reads the title's variables as often as the process allows: every change of the camera record (0x131ea4: the angles a0 a1 a2 in
2048ths, then the place x, y up, z), the frame time (0x133350), the hundredths (0x4cfd6), the page's flags (0x132494 ..) and the four cars' object records (the number
in the table 0x130fe0, 8 bytes a car, the record pointer at 0x11da20 + 0x1c x the number: +8 x, +0xc y up, +0x10 z, +0x14 .. +0x1c the angles, 0x40 bytes read) is a line of
build/check/dosbox_scene/frames.jsonl (never committed). Raw screenshots (Ctrl+F5, 320 x 200 palette indices) are taken every 1.3 s from the 3rd second on; the time of each
is a line of the recording, so that the frame it shows can be found. tools/host/scene_match.c (tools/scene.sh) reads both.

Only the DOSBox this script started is keyed and read (found by its configuration file); another one running (the owner playing) is never touched. The window must stay
in front for the run (about 2 minutes). The captures go into build/check, not into the game folder.
"""
import json
import os
import struct
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402

CAM, FLAGS, DT, HUND = 0x131EA4, 0x132494, 0x133350, 0x4CFD6
CAR_TABLE, OBJECTS = 0x130FE0, 0x11DA20         # 8 bytes a car (the first word: the engine's object number); a record pointer at 0x11da20 + 0x1c x the number


def record(step, work, mem, shots, times):
    """S<seconds>[:s<label>@<t> ...] (a step of dosbox_arcade.session): the recording described in the header"""
    f = step.split(":")
    secs = float(f[0])
    events = sorted((float(e.split("@")[1]), e.split("@")[0]) for e in f[1:] if e[0] == "s")
    g = mem.g
    da.front(da.keyed_pid)
    ptrs = []
    for i in range(4):
        n = struct.unpack("<i", g.flat(CAR_TABLE + 8 * i, 4))[0]
        ptrs.append(struct.unpack("<I", g.flat(OBJECTS + 0x1C * n, 4))[0])
    out = open(os.path.join(work, "frames.jsonl"), "w")
    out.write(json.dumps({"cars": ptrs, "table": g.flat(CAR_TABLE, 0x50).hex()}) + "\n")
    t0, last = time.time(), None
    while time.time() - t0 < secs:
        now = time.time() - t0
        while events and events[0][0] <= now:
            _, what = events.pop(0)
            if da.foreground_pid() != da.keyed_pid:
                da.front(da.keyed_pid)
            times.append(time.time())
            shots.append(what[1:])
            for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                da.key(sc, up)
                time.sleep(0.02)
            out.write(json.dumps({"shot": what[1:], "t": round(now, 4)}) + "\n")
        cur = (g.flat(CAM, 24), g.flat(DT, 4), g.flat(HUND, 4), g.flat(FLAGS, 28), [g.ptr(p, 0x40) for p in ptrs])
        if cur != last:
            out.write(json.dumps({"t": round(time.time() - t0, 4), "cam": cur[0].hex(), "dt": cur[1].hex(), "hund": cur[2].hex(), "flags": cur[3].hex(),
                                  "cars": [c.hex() for c in cur[4]]}) + "\n")
            last = cur
    out.close()
    print("recorded %.0f s" % (time.time() - t0))


da.HOOKS["S"] = record


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 45.0
    da.work = os.path.join(da.root, "build", "check", "dosbox_scene")
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(0, team=0, car=1, name="MIL")
    # the attract sequence plays while the game's memory is looked for (M); ENTER: TEAM SELECTION, ENTER: the name, ENTER: the options room, ENTER on its first station
    # (ARCADE): the title, whose cars drive at once
    shots = "".join(":s%d@%.1f" % (i, 3 + 1.3 * i) for i in range(int((secs - 6) / 1.3)))
    steps = "w3 M k1c w6 k1c w6 k1c w6 k1c w4 S%g%s q" % (secs, shots)
    got = da.session(steps)
    print("%d screenshots in %s" % (len(got), da.work))


if __name__ == "__main__":
    main()
