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
    if kind in "JK":
        league, f = int(f[0]), f[1:]
    secs = float(f[0])
    events = sorted((float(e.split("@")[1]), e.split("@")[0]) for e in f[1:] if e[0] == "s")
    g = mem.g
    da.front(da.keyed_pid)
    out = open(os.path.join(work, "frames.jsonl"), "a")
    if kind == "K":
        # the LEAGUE REPORT after a race (the page of 0x43ade): its state word 0x1324c4 is 1 (won, the page shows CONGRATULATIONS) and ENTER goes to the desk scene (0x44144, as a won league's
        # last ENTER does); league `league` is the league in hand
        g.poke(0x1324C4, struct.pack("<i", 1))
        if league:                                          # (league 4: the second visit after league 4, scene 4: the fifth car is the team's chosen one, which the first visit's end sets)
            g.poke(LEAGUE, struct.pack("<i", league))
            g.poke(TEAMS, struct.pack("<i", 4))
        da.press(0x1C)
        out.write(json.dumps({"poke": league, "t": 0, "ok": True}) + "\n")
    elif league is not None:
        # the way of the LEAGUE REPORT after a win is its ENTER, which sets the next page to the desk scene (0x44144); here the page we are on, LEAGUE SELECTION, is left the way it
        # leaves for WELCOME (ENTER on an open league: it sets the next page and fades out, and its own tail runs), and only the next page it set is replaced by the desk scene's
        word = lambda a: struct.unpack("<i", g.flat(a, 4))[0]
        while word(LEAGUE) != league:                      # the cursor to the league (LEFT / RIGHT, as a player would)
            da.press(0x4D if word(LEAGUE) < league else 0x4B, True)
            time.sleep(0.4)
        # (the page's fade is 25 frames, a few hundredths of a second in DOSBox, and WELCOME clears the word at once: the desk scene's address is written all the while, which is only
        # undone by the page's own ENTER writing WELCOME's, so after the key the word is the desk scene's as the page ends)
        da.key(0x1C, 0)
        t_wait, ok, released = time.time(), True, False
        while time.time() - t_wait < 1.0:
            ok = g.poke(FLAGS, struct.pack("<I", 0x3BC38)) and ok
            if not released and time.time() - t_wait > 0.15:
                da.key(0x1C, 1)
                released = True
        out.write(json.dumps({"poke": league, "t": 0, "ok": bool(ok)}) + "\n")
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
        if None in head:                                    # the game is gone (it can stop on a page its state does not suit): what was recorded stays
            out.close()
            sys.exit("the DOSBox stopped: %d bytes recorded" % os.path.getsize(os.path.join(work, "frames.jsonl")))
        if kind in "JK":
            tab = g.flat(SCENE_OBJS, 32)
            vars_ = g.flat(SCENE_VARS, 24)
            count = struct.unpack_from("<i", vars_, 0)[0]          # (the engine's first object is number 0: the car of a scene is object 0 where nothing else was loaded)
            objs = [struct.unpack_from("<i", tab, 8 * k)[0] for k in range(min(max(count, 0), 4))]
            cur = head + (vars_, g.flat(LEAGUE, 4), tab, rows(g, [n for n in objs if 0 <= n < 48]))
        else:
            tab = g.flat(PLATE_TABLE, 56)
            objs = [struct.unpack_from("<i", tab, 8 * k)[0] for k in range(7)]
            cur = head + (g.flat(PLATES, 0x4C * 8), tab, rows(g, [n for n in objs if 0 < n < 48]))
        if cur != last:
            row = {"t": round(time.time() - t0, 4), "cam": cur[0].hex(), "dt": cur[1].hex(), "hund": cur[2].hex(), "flags": cur[3].hex(), "anim": cur[4].hex()}
            if kind in "JK":
                row.update({"vars": cur[5].hex(), "league": cur[6].hex(), "tab": cur[7].hex(), "objs": cur[8]})
            else:
                row.update({"plates": cur[5].hex(), "tab": cur[6].hex(), "objs": cur[7]})
            out.write(json.dumps(row) + "\n")
            last = cur
    out.close()
    print("recorded %.0f s" % (time.time() - t0))


def to_selection(step, work, mem, shots, times):
    """H: from the attract sequence to LEAGUE SELECTION: ENTER (TEAM SELECTION), ENTER (the name), ENTER (the options room) as long as the room has not been entered (its table of
    stations, P_<t>.MCM, is in the game's memory at 0x131f34 from its start: 5 stations), then RIGHT until the room's station (0x132378) is CHAMPIONSHIP (1) and ENTER: the room's next
    page is LEAGUE SELECTION (0x4337d, 0x390e4)"""
    g = mem.g
    word = lambda a: struct.unpack("<i", g.flat(a, 4))[0]
    da.front(da.keyed_pid)
    for _ in range(8):
        if word(0x131F34) == 5:
            break
        da.press(0x1C)
        time.sleep(4.5)
    if word(0x131F34) != 5:
        sys.exit("the options room did not come up")
    time.sleep(3.0)                                     # (its camera flies in)
    for _ in range(8):
        if word(0x132378) == 1:
            break
        da.press(0x4D, True)
        time.sleep(1.0)
    if word(0x132378) != 1:
        sys.exit("the options room is not at CHAMPIONSHIP")
    time.sleep(2.0)
    da.press(0x1C)
    t0 = time.time()
    while time.time() - t0 < 10 and word(0x132494) != 0x4337D:
        time.sleep(0.05)
    print("the room leads to page 0x%x" % word(0x132494))


da.HOOKS["H"] = to_selection
da.HOOKS["V"] = lambda *a: record("V", *a)
da.HOOKS["J"] = lambda *a: record("J", *a)
da.HOOKS["K"] = lambda *a: record("K", *a)


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("welcome", "desk", "report"):
        sys.exit(__doc__)
    team = int(a[1]) if len(a) > 1 else 1
    da.work = os.path.join(da.root, "build", "check", "dosbox_welcome" if a[0] == "welcome" else "dosbox_desk")
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(0, team=team - 1, car=1, name="MIL", league=3, laps=0)
    # the attract sequence plays while the game's memory is looked for (M); ENTER: TEAM SELECTION, ENTER: the name, ENTER: the options room, RIGHT, ENTER on CHAMPIONSHIP:
    # LEAGUE SELECTION, one raw frame of it (the page the room leads to)
    prelude = "w3 M H w5 Lselection r1,0.3 "
    if a[0] == "welcome":
        secs = float(os.environ.get("SECS", "40"))
        shots = "".join(":s%d@%.1f" % (i, 3.5 + 0.7 * i) for i in range(int((secs - 6) / 0.7)))
        steps = prelude + "k1c w0.2 V%g%s q" % (secs, shots)
    elif a[0] == "report":
        # the natural way to the desk scene: a league race of one lap (the player's car handed to the game's own driver, tools/s2/dosbox_arcade.py league()), its end screen, the LEAGUE
        # REPORT, whose state word is set to "won" (a real win needs the whole league): ENTER leads to the desk scene as it does after a win; league 1 (the scene 1) is what the report has
        secs = float(os.environ.get("SECS", "26"))
        shots = "".join(":s%d@%.1f" % (i, 4 + 1.2 * i) for i in range(int((secs - 6) / 1.2)))
        first = int(a[2]) if len(a) > 2 else 0               # the league in hand when the report's ENTER is taken (0 .. 3: the scene of that league; 4: scene 4)
        secs = float(os.environ.get("SECS") or {0: 26, 1: 32, 2: 32, 3: 32, 4: 40}[first])
        shots = "".join(":s%d@%.1f" % (i, 4 + 1.2 * i) for i in range(int((secs - 6) / 1.2)))
        steps = prelude + "k1c w3 k1c w7 k1c w6 k1c w2 k1c w2 Xrace Xfin w5 ke4d w0.6 k1c w5 K%d:%g%s " % (first, secs, shots)
        for lg in [int(x) for x in a[3:]]:          # (more scenes by the selection the first returns to: the poke of the selection's next page crashed the game in every try)
            secs = float(os.environ.get("SECS") or {1: 28, 2: 28, 3: 62}.get(lg, 30))
            shots = "".join(":s%d@%.1f" % (i, 4 + 1.2 * i) for i in range(int((secs - 6) / 1.2)))
            steps += "J%d:%g%s " % (lg, secs, shots)
        steps += "q"
    else:
        leagues = [int(x) for x in a[2:]] or [0]
        # seconds of a scene's recording: its load (a few seconds in DOSBox), the scene (6, 8, 8, 12 s at 50 frames a second) and its fade; league 3 is followed by the fifth car's scene
        long = {0: 22, 1: 28, 2: 28, 3: 62}
        steps = prelude
        for lg in leagues:
            secs = float(os.environ.get("SECS") or long.get(lg, 30))
            shots = "".join(":s%d@%.1f" % (i, 4 + 1.2 * i) for i in range(int((secs - 6) / 1.2)))
            steps += "J%d:%g%s " % (lg, secs, shots)
        steps += "q"
    print(steps)
    got = da.session(steps)
    print("%d screenshots in %s" % (len(got), da.work))


if __name__ == "__main__":
    main()
