"""Three small things of the running original, read from its memory and a screenshot (docs/screamer2/menus.md "The end screen's sounds", development.md "The windows",
menus.md "The arcade grid"): the effects' master volume at the end screen, the grid of an arcade race, and the palette a race shows.

Usage: python -I tools/s2/dosbox_polish.py <track 1..7> [--fade]       (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")
       python -I tools/s2/dosbox_polish.py <track 1..7> --check         (the last run's files again, without DOSBox)

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (tools/s2/dosbox_arcade.py make_private: build/dosgame, the data copied, never linked; the
owner's game folder is only read) whose CHOICE.DAT is team 3 (WASP), its second car, all seven tracks open, ONE lap; types its way to an arcade race on the track (as dosbox_anim.py
does), and on the grid:
  - the four cars' entries (0x130c58 + 0x2c i: the team and the car of grid slot i, 0-based) go to bytes_grid.txt: the rule of 0x33b0e .. 0x33bc9 is `(team + k) mod 4` and
    `(car + k) mod 4`; with team 2 and car 1 that is teams 2 3 0 1 and cars 1 2 3 0;
  - three screenshots (Ctrl+F5, 320 x 200 raw frame): the palette is the DAC's, which --check compares with `MAP<n>.COL` and with `ENV.COL` of `GFX/ENVP<n>01.DAT` (the original
    never reads ENV.COL: 0x2ee14 .. 0x2ee9b loads the track's `%s.COL`, name `map<n>` from 0x2e680);
  - with --fade: the player's car is handed to the game's driver (as dosbox_arcade.py --replay does), the race runs its lap, and from the finish on the sound manager's master
    volume (the object at the pointer 0x124274, its +0x18) and the end screen's clock (0x133050, seconds since the finish) are read as fast as the process allows for 12 s into
    fade.txt: the end screens (0x47636, 0x48041, 0x48b4c) ease the master towards 0 by (int)(dt x 16383) a frame once the clock has passed 4.0 (0xaee60, called with the target 0
    and that step).
Only the DOSBox this script started is keyed and read (found by its configuration file); another one running (the owner's, another agent's) stops the run at once and nothing is
sent to it. The window must stay in front for the run (about 4 minutes). Output in build/check/dosbox_polish_t<n>/ (never committed).
"""
import json
import os
import struct
import sys
import threading
import time
import zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da                      # noqa: E402
import dosbox_anim as anim                      # noqa: E402  (its watchdog and the list of the DOSBoxes running)

sys.path.insert(0, os.path.join(da.root, "tools"))
import datfile                                  # noqa: E402

GRID = (0x130C58, 4 * 0x2C)                     # the four cars' entries: team (+0), car (+4) of grid slot i, 0-based
TEAM, CAR = 0x1289EC, 0x128A9C                  # the player's team; the teams' records of 0x188 bytes, whose first word is the chosen car
MASTER_PTR, CLOCK, DT = 0x124274, 0x133050, 0x133350
TEAM0, CAR0 = 2, 1                              # the private CHOICE.DAT: WASP, its second car


def grid(step, work, mem, shots, times):
    """G: the entries of the grid, the player's team and chosen car, and the sound manager's master volume (+0x18) and the settings' effects volume 0x130d58 to bytes_grid.txt"""
    g = mem.g
    ptr = struct.unpack("<I", g.flat(MASTER_PTR, 4))[0]
    rows = ["entries " + g.flat(*GRID).hex(), "team %d" % struct.unpack("<i", g.flat(TEAM, 4))[0],
            "car %d" % struct.unpack("<i", g.flat(CAR + 0x188 * TEAM0, 4))[0],
            "master %d" % struct.unpack("<i", g.ptr(ptr + 0x18, 4))[0], "setting %d" % struct.unpack("<i", g.flat(0x130D58, 4))[0],
            "mode %d level %d" % (struct.unpack("<i", g.flat(0x130D10, 4))[0], struct.unpack("<i", g.flat(0x130D4C, 4))[0])]
    open(os.path.join(work, "bytes_grid.txt"), "w").write("\n".join(rows) + "\n")


da.HOOKS["G"] = grid


def drive(step, work, mem, shots, times):
    """H: the player's car to the game's driver (its controls pointer, +0xb4 of its record, set to 0), as Mem.race does"""
    print("the player's car handed to the game's driver:", "yes" if mem.g.poke(0x1193B4, struct.pack("<i", 0)) else "NO")


da.HOOKS["H"] = drive


def watch(step, work, mem, shots, times):
    """E<seconds>: the master volume, the end screen's clock and the frame time as fast as the process allows, a row when any changes: the time since the call, the clock, the
    master, dt"""
    secs = float(step)
    g = mem.g
    ptr = struct.unpack("<I", g.flat(MASTER_PTR, 4))[0]
    rows, last, t0 = [], None, time.time()
    while time.time() - t0 < secs:
        cur = (struct.unpack("<f", g.flat(CLOCK, 4))[0], struct.unpack("<i", g.ptr(ptr + 0x18, 4))[0], struct.unpack("<f", g.flat(DT, 4))[0])
        if cur != last:
            rows.append([round(time.time() - t0, 4), *cur])
            last = cur
    json.dump(rows, open(os.path.join(work, "fade.txt"), "w"))
    print("fade: %d rows" % len(rows))


da.HOOKS["E"] = watch


def png_palette(path):
    """the PLTE of a PNG: 256 (r, g, b)"""
    d = open(path, "rb").read()
    p = 8
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"PLTE":
            return [tuple(d[p + 8 + 3 * i:p + 11 + 3 * i]) for i in range(n // 3)]
        p += 12 + n
    return None


def check(track, work):
    """the files of the last run against the original's data: the grid, the palette, the fade. Returns the number of things that are not as the program's code says"""
    bad = 0
    R = da.game
    g = datfile.read("%s/GFX/MAP%dH01.DAT" % (R, track))
    col = datfile.extract(g, "MAP%d.COL" % track)
    ecol = datfile.extract(datfile.read("%s/GFX/ENVP%d01.DAT" % (R, track)), "ENV.COL")
    differ = sum(col[3 * i:3 * i + 3] != ecol[3 * i:3 * i + 3] for i in range(256))
    print("MAP%d.COL and ENV.COL differ in %d of 256 entries" % (track, differ))
    # the grid
    f = dict(l.split(" ", 1) for l in open(os.path.join(work, "bytes_grid.txt")).read().splitlines())
    ent = bytes.fromhex(f["entries"])
    got = [(struct.unpack_from("<i", ent, 0x2C * i)[0], struct.unpack_from("<i", ent, 0x2C * i + 4)[0]) for i in range(4)]
    want = [((TEAM0 + k) % 4, (CAR0 + k) % 4) for k in range(4)]
    print("grid (team, car) of slots 0 .. 3:", got, "the rule (team + k) mod 4, (car + k) mod 4:", want, "OK" if got == want else "NOT EQUAL")
    bad += got != want
    print("master volume at the grid %s, the settings' effects volume 0x130d58 %s, kind of game / level: %s" % (f["master"], f["setting"], f["mode"]))
    # the palette of every screenshot: the DAC's, 6 bits an entry, against the track's and ENV.COL's
    for png in sorted(x for x in os.listdir(work) if x.endswith(".png")):
        pal = png_palette(os.path.join(work, png))
        dt = sum(max(abs((pal[i][c] >> 2) - col[3 * i + c]) for c in range(3)) > 1 for i in range(256))
        de = sum(max(abs((pal[i][c] >> 2) - ecol[3 * i + c]) for c in range(3)) > 1 for i in range(256))
        print("%s: palette entries away from MAP%d.COL: %d, from ENV.COL: %d (of 256)" % (png, track, dt, de))
        bad += dt > 8 or (differ > 100 and de < 100)
    if os.path.exists(os.path.join(work, "fade.txt")):
        rows = json.load(open(os.path.join(work, "fade.txt")))               # [seconds since the call, the end screen's clock, the master, dt]
        seq = []                                                             # the distinct values of the master, in order: (clock, master, dt)
        for r in rows:
            if not seq or seq[-1][1] != r[2]:
                seq.append((r[1], r[2], r[3]))
        print("fade: %d rows; the master is %d at the finish (clock %.3f)" % (len(rows), seq[0][1], rows[0][1]))
        if len(seq) < 3:
            print("      the master did not move")
            return bad + 1
        steps = [seq[i][1] - seq[i + 1][1] for i in range(len(seq) - 1)]
        want = [int(seq[i + 1][2] * 16383 + 0.5) for i in range(len(seq) - 1)]       # (int)(dt x 16383), fistp rounding to nearest, with the frame's dt
        full = [i for i in range(len(steps) - 1) if steps[i] == want[i]]
        print("      the master first moves at clock %.3f (until then it stays %d), is 0 at clock %.3f: %d frames, %.3f s of the game's clock" %
              (seq[1][0], seq[0][1], seq[-1][0], len(seq) - 1, seq[-1][0] - seq[1][0] + seq[1][2]))
        print("      steps of the ease: %d of %d frames take (int)(dt x 16383) = %d (dt %.4f), the last takes %d (it clamps at 0 as 0xaee60 does)" %
              (len(full), len(steps), steps[0], seq[1][2], steps[-1]))
        ok = seq[1][0] >= 4.0 and seq[1][0] < 4.1 and len(full) == len(steps) - 1 and seq[-1][1] == 0 and 0 < steps[-1] <= steps[0]
        print("      " + ("as 0xaee60 and the end screens' callers have it" if ok else "NOT AS THE CODE HAS IT"))
        bad += not ok
    return bad


def main():
    if len(sys.argv) < 2 or not sys.argv[1].isdigit() or not 1 <= int(sys.argv[1]) <= 7:
        sys.exit(__doc__)
    track = int(sys.argv[1])
    da.work = os.path.join(da.root, "build", "check", "dosbox_polish_t%d" % track)
    if "--check" in sys.argv:
        sys.exit(1 if check(track, da.work) else 0)
    if anim.others():
        sys.exit("a DOSBox is running (the owner's, or another agent's): not started, wait until it is gone")
    threading.Thread(target=anim.watchdog, daemon=True).start()
    import atexit
    atexit.register(da.drop_junctions)
    da.make_private(4, team=TEAM0, car=CAR0, name="MIL", laps=0, detail=2)         # all seven tracks open, one lap
    pos = da.ORDER.index(track)
    steps = "w3 M k1c w6 k1c w6 k1c w6 k1c w9 k1c w4 " + "ke4b w1.3 " * 7 + "ke4d w1.3 " * pos + "k1c w5 k1c w2 k1c w3 "
    steps += "Aarc Lgrid r3,0.5 G "                  # the race has run (the lights are out) and the car stands: the entries, three screenshots
    if "--fade" in sys.argv:
        steps += "H Xfin E12 Lend r1,0.5 "
    got = da.session(steps + "q")
    print("%d screenshots in %s" % (len(got), da.work))
    sys.exit(1 if check(track, da.work) else 0)


def arc(step, work, mem, shots, times):
    """A: the arcade race has run for 20 s, the car left to the keys (nobody presses any)"""
    mem.race(work, 1, None, False)


da.HOOKS["A"] = arc

if __name__ == "__main__":
    main()
