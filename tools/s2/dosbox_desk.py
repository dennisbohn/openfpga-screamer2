"""The championship's desk scene and WELCOME's flying plates against the running original (docs/screamer2/menus.md "The desk scene", "WELCOME's plates").

Usage: python -I tools/s2/dosbox_desk.py welcome [team]            WELCOME TO LEAGUE 1: every frame of the plates, raw screenshots
       python -I tools/s2/dosbox_desk.py desk [team] [league ...]   the desk scene of the given leagues (0 .. 3 and 4, the second visit of the fourth: the prize car's)
                                                                    (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2"; the window of the DOSBox it starts must stay in front)

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame, the data copied, never linked, the
owner's folder is only read; team `team` (1 .. 4, default 1) named MIL, the team's second car chosen), types its way through TEAM SELECTION, the name and the OPTIONS ROOM to
CHAMPIONSHIP and ENTER: LEAGUE SELECTION (STARTL.EXE 0x4337d; the room leads there at once, there is no scene between them, 0x39116). `welcome` then takes ENTER on league 1 and records
WELCOME (0x39569) until its plates have flown; `desk` pokes the page variables of the selection (the league in hand 0x128a10, the next page 0x132494 = the desk scene 0x3bc38, the
done flag 0x1324a0) for each league, which is what the LEAGUE REPORT's last ENTER does after a win (0x44144), and records the scene.

Every change of what the page keeps in memory is a line of build/check/dosbox_desk/frames.jsonl (never committed): the camera record (0x131ea4: angles a0 a1 a2 in 2048ths, then the place
x, y up, z), the frame time (0x133350), the page's hundredths (0x4cfd6, two a frame from 0: the frame number), the page's flags (0x132494 .. 0x1324b0), the animation clock (0x1347a0),
the scene's variables (0x1192d0 ..: objects, camera paths, the two durations of SPLINES.TXT, the scene number, the wheels' phase) or the plates' records (0x131290, 0x4c bytes a plate,
the table 0x130fa8) and the 0x40-byte records of the objects the page draws (the engine's table 0x11da14, 0x1c bytes an object, the record pointer at +0xc). Raw screenshots (Ctrl+F5,
320 x 200 palette indices) are taken every ~1 s from the 3rd second on, the time of each is a line of the recording. tools/host/desk_match.c (tools/desk.sh) reads both.

Only the DOSBox this script started is keyed and read and written (found by its configuration file); another one running (the owner playing) is never touched. Take the lock folder first.
"""
import json
import os
import struct
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402

CAM, DT, HUND, FLAGS, ANIM = 0x131EA4, 0x133350, 0x4CFD6, 0x132494, 0x1347A0
SCENE_VARS, SCENE_OBJS = 0x1192D0, 0x11918C     # count of objects and camera paths, the two durations, the scene number, the wheels' phase; the object numbers (8 bytes a row)
PLATES, PLATE_TABLE = 0x131290, 0x130FA8        # the plates' records (0x4c bytes) and the table of the loaded meshes (8 bytes a row)
ENGINE = 0x11DA14                               # the engine's objects, 0x1c bytes each, the record's pointer at +0xc
LEAGUE, TEAM, TEAMS = 0x128A10, 0x1289EC, 0x128A9C


def rows(g, ptr_n):
    """the records (0x40 bytes) of the engine's objects n in ptr_n, as hex ('' for one that is not there yet)"""
    eng = g.flat(ENGINE, 0x1C * 48)
    out = []
    for n in ptr_n:
        p = struct.unpack_from("<I", eng, 0x1C * n + 0xC)[0] if 0 <= n < 48 else 0
        rec = g.ptr(p, 0x40) if 0x10000 <= p < 0x8000000 else None
        out.append(rec.hex() if rec else "")
    return out


def record(kind, step, work, mem, shots, times):
    """<secs>[:s<label>@<t> ...] (a step of dosbox_arcade.session) for `kind` 'V' (WELCOME) or 'J' (the desk scene: J<league>:<secs>...)"""
    f = step.split(":")
    league = None
    if kind == "J":
        league, f = int(f[0]), f[1:]
    secs = float(f[0])
    events = sorted((float(e.split("@")[1]), e.split("@")[0]) for e in f[1:] if e[0] == "s")
    g = mem.g
    da.front(da.keyed_pid)
    out = open(os.path.join(work, "frames.jsonl"), "a")
    if league is not None:                      # the LEAGUE REPORT's way to the desk scene: next page 0x3bc38, the page we are on is done
        g.poke(LEAGUE, struct.pack("<i", league))
        g.poke(FLAGS, struct.pack("<I", 0x3BC38))
        g.poke(FLAGS + 0xC, struct.pack("<I", 0x3F800000))
        out.write(json.dumps({"poke": league, "t": 0}) + "\n")
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
        head = (g.flat(CAM, 24), g.flat(DT, 4), g.flat(HUND, 4), g.flat(FLAGS, 28), g.flat(ANIM, 4))
        if kind == "J":
            tab = g.flat(SCENE_OBJS, 32)
            objs = [struct.unpack_from("<i", tab, 8 * k)[0] for k in range(4)]
            cur = head + (g.flat(SCENE_VARS, 24), g.flat(LEAGUE, 4), tab, rows(g, [n for n in objs if 0 < n < 48]))
        else:
            tab = g.flat(PLATE_TABLE, 56)
            objs = [struct.unpack_from("<i", tab, 8 * k)[0] for k in range(7)]
            cur = head + (g.flat(PLATES, 0x4C * 8), tab, rows(g, [n for n in objs if 0 < n < 48]))
        if cur != last:
            row = {"t": round(time.time() - t0, 4), "cam": cur[0].hex(), "dt": cur[1].hex(), "hund": cur[2].hex(), "flags": cur[3].hex(), "anim": cur[4].hex()}
            if kind == "J":
                row.update({"vars": cur[5].hex(), "league": cur[6].hex(), "tab": cur[7].hex(), "objs": cur[8]})
            else:
                row.update({"plates": cur[5].hex(), "tab": cur[6].hex(), "objs": cur[7]})
            out.write(json.dumps(row) + "\n")
            last = cur
    out.close()
    print("recorded %.0f s" % (time.time() - t0))


da.HOOKS["V"] = lambda *a: record("V", *a)
da.HOOKS["J"] = lambda *a: record("J", *a)


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("welcome", "desk"):
        sys.exit(__doc__)
    team = int(a[1]) if len(a) > 1 else 1
    da.work = os.path.join(da.root, "build", "check", "dosbox_desk")
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(0, team=team - 1, car=1, name="MIL", league=3, laps=0)
    # the attract sequence plays while the game's memory is looked for (M); ENTER: TEAM SELECTION, ENTER: the name, ENTER: the options room, RIGHT, ENTER on CHAMPIONSHIP:
    # LEAGUE SELECTION, one raw frame of it (the page the room leads to)
    prelude = "w3 M k1c w6 k1c w6 k1c w6 ke4d w4 k1c w6 Lselection r1,0.3 "
    if a[0] == "welcome":
        secs = float(os.environ.get("SECS", "40"))
        shots = "".join(":s%d@%.1f" % (i, 3.5 + 0.7 * i) for i in range(int((secs - 6) / 0.7)))
        steps = prelude + "k1c w0.2 V%g%s q" % (secs, shots)
    else:
        leagues = [int(x) for x in a[2:]] or [0]
        secs = float(os.environ.get("SECS", "40"))
        shots = "".join(":s%d@%.1f" % (i, 4 + 1.2 * i) for i in range(int((secs - 6) / 1.2)))
        steps = prelude + " ".join("J%d:%g%s" % (lg, secs, shots) for lg in leagues) + " q"
    print(steps)
    got = da.session(steps)
    print("%d screenshots in %s" % (len(got), da.work))


if __name__ == "__main__":
    main()
