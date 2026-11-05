"""The original's own race in DOSBox, four cars frame by frame: what the reference (OpenScreamer's routines) of tools/s2/check_aicontact_s2.sh is held against.

Usage: python -I tools/s2/dosbox_race_s2.py <track 1..7> [seconds of race, default 30]            (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")
       python -I tools/s2/dosbox_race_s2.py --compare <track>                                      the recording against the reference's routines run with its steps

Starts STARTL.EXE in the game folder's own DOSBox on the private copy of the game folder of tools/s2/dosbox_arcade.py (build/dosgame; the captures go to build/check/dosbox_race),
types its way (the keys of the arcade pages there) to an ARCADE race on the track chosen with the first team's second car, the game's own menu choices (the other three cars
its own), and leaves the player's car alone on the grid. The keys go only to the DOSBox this script started, and only while its window is the one in front (else it stops);
it never looks for "the first DOSBox" and does not touch another. From the start of the race the game's memory (tools/dosram.py finds it in DOSBox's process) is looked at as
often as it can be: a frame's state is the last look before the race clock 0x13335c moves again (the clock goes on at a frame's start; the cars do not change after the
frame's physics until the next one): the clock, the step the routines work with 0x133350 and the raw time 0x133354 (the game has no fixed step: it runs a picture at a time,
50 a second in this DOSBox, and the step is 1.1 x the time the last pictures took, so 0.022 although the clock goes by 0.02), the count of steps cut short by a touch
0x11cdb4, and the four cars' whole records (0xCC0 bytes from 0x119300). Writes build/check/dosbox_race/race.t<track>.bin (the records of every frame, the format of the
reference's traces; its row i is the state after the frame that brought the clock to i x 0.02) and race.t<track>.frames.txt (a line a frame: clock, step, raw, touch count).
Needs DOSBox's window in front for the keys (about 60 s), then not.

--compare runs the reference with the recorded steps (OS_S2SCRIPT: "step raw acc brk left right" a frame, the controls all 0, OS_S2HOLD for the grid's 3 s) and prints the
cars' errors against the recording. The reference's program is <reference folder>/build-src (see tools/s2/ref_trace).
"""
import os
import struct
import subprocess
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da          # (its keys, its private game folder, its way to the game's memory)

root = da.root
out = os.path.join(root, "build", "check", "dosbox_race")
REC = 0xCC0
CARS = 0x119300


def foreground_pid():
    """the process of the window in front"""
    pr = da.wt.DWORD()
    da.user32.GetWindowThreadProcessId(da.user32.GetForegroundWindow(), da.ctypes.byref(pr))
    return pr.value


def other_games():
    """the DOSBox windows of other runs that have the game open (a human playing, another script): the keys of this script go to the window in front and must not reach them"""
    r = subprocess.run(["powershell", "-NoProfile", "-Command", "Get-Process dosbox -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like '*STARTL*' } | "
                        "ForEach-Object { $_.Id }"], capture_output=True, text=True)
    return [int(x) for x in r.stdout.split()]


def start(track, seconds):
    busy = other_games()
    if busy:
        sys.exit("another DOSBox has the game open (process %s): its window and this script's keys would collide; close it or run later" % busy)
    da.work = out
    os.makedirs(out, exist_ok=True)
    da.atexit.register(da.drop_junctions)
    da.make_private(4, team=0, car=1, name="MIL")        # all seven tracks open; the first team's second car (the reference's player), the name MIL
    conf = os.path.join(out, "run.conf")
    open(conf, "w").write('[sdl]\nfullscreen=false\nwindowresolution=original\n[dosbox]\ncaptures=%s\n[autoexec]\n@echo off\nmount C "%s"\n'
                          'imgmount d "%s/GAME.DAT" -t iso\nc:\nSTARTL.EXE\nexit\n' % (out, da.private, da.game))
    p = subprocess.Popen([da.game + "/DOSBOX/dosbox.exe", "-conf", da.game + "/dosbox_screamer2.conf", "-conf", conf, "-noconsole"], cwd=out)
    da.atexit.register(p.kill)
    time.sleep(2)
    pid = p.pid                                          # (this script's own DOSBox, not the first one in the process list)
    # the attract race plays while the game's memory is looked for; ENTER leaves it: TEAM SELECTION, the name, the options room, ARCADE, the TRACK page (RIGHT to the
    # track: the pages' order is da.ORDER), the CAR page, the TRANSMISSION page, the race
    mem = False
    for _ in range(25):                                  # (the game is loaded and its data object in DOSBox's memory after a few seconds; the attract race plays meanwhile)
        time.sleep(4)
        mem = da.Mem.open(pid)
        if mem:
            break
    if not mem:
        sys.exit("the game's memory was not found")
    steps = "w9 k1c w6 k1c w6 k1c w6 k1c w9 k1c w4 " + "ke4d w2.5 " * da.ORDER.index(track) + "k1c w5 k1c w1.5 k1c"
    for st in steps.split():
        if st[0] == "w":
            time.sleep(float(st[1:]))
        else:
            # keys go to the window in front and only this script's own DOSBox (the process it started) may be there: stop if it is gone or does not get the front
            if p.poll() is not None:
                sys.exit("this script's DOSBox (process %d) has ended" % pid)
            da.front(pid)
            if foreground_pid() != pid:
                sys.exit("this script's DOSBox (process %d) is not the window in front (process %d): no key is sent" % (pid, foreground_pid()))
            ext = st[1] == "e"
            da.press(int(st[2:] if ext else st[1:], 16), ext)
    return p, mem


def f32(b):
    return struct.unpack("<f", b)[0]


def record(mem, track, seconds):
    """looks at the game's memory as fast as it can; a frame's state is the last look before the race clock moves again (the clock goes on at the frame's start, its
    physics is done soon after, and nothing changes the cars until the next frame): the clock, the step and the raw time, the count of steps cut short by a touch and
    the cars' records of a look are taken together and kept only if the clock did not move meanwhile"""
    g = mem.g
    ok = mem.wait(lambda: mem.word(0x130d10) == 1 and mem.word(0x130c48) == 4 and f32(g.flat(0x13335c, 4)) < 1.0, 200)     # (the attract race's clock may still be on)
    if not ok:
        sys.exit("the race did not start")
    open(os.path.join(out, "race.t%d.level.txt" % track), "w").write(str(mem.word(0x130d4c)))
    print("the cars of the race (team, model) as the game has them:", [(mem.word(0x130c58 + 0x2c * i), mem.word(0x130c5c + 0x2c * i)) for i in range(4)],
          "level", mem.word(0x130d4c), "mode", mem.word(0x130d10))
    last, cur, n = None, None, 0
    bin_path = os.path.join(out, "race.t%d.bin" % track)
    first = clock_end = 0.0
    with open(bin_path, "wb") as f, open(os.path.join(out, "race.t%d.frames.txt" % track), "w") as t:
        while True:
            c0 = f32(g.flat(0x13335c, 4))
            sample = (c0, f32(g.flat(0x133350, 4)), f32(g.flat(0x133354, 4)), mem.word(0x11cdb4), g.flat(CARS, 4 * REC))
            if f32(g.flat(0x13335c, 4)) != c0:
                continue                                     # (the frame began while looking)
            if last is not None and c0 < last - 1.0:
                break                                        # the clock started again: a new race
            if c0 != last:
                if cur is not None:
                    t.write("%.9g %.9g %.9g %d" % cur[:4] + chr(10))
                    f.write(cur[4])
                    n += 1
                    if n == 1:
                        first = cur[0]
                    clock_end = cur[0]
                    if cur[0] > seconds:
                        break
                last = c0
            cur = sample
    print("%d frames, race clock %.2f .. %.2f s" % (n, first, clock_end))


def compare(track):
    ref = os.environ.get("REF_DIR", os.path.join(root, "..", "ref"))
    exe = os.path.join(ref, "build-src", "port", "build", "screamer-remastered.exe")
    frames = [l.split() for l in open(os.path.join(out, "race.t%d.frames.txt" % track))]
    # a row of the recording is the state after the frame that brought the clock to the row's value, so the first row (the clock still at 0) is the grid before any frame; if the
    # recording missed a frame (the clock moved twice between two looks) the reference is run through it with the step and raw time of the next row; `at` is the
    # reference's frame (-1: none yet) of each recorded row
    script, at, prev = [], [], 0.0
    for c, step, raw, touch in frames:
        n = int(round((float(c) - prev) / float(raw)))      # (a row of the recording is the state after the frame that brought the clock to its value: frame k = clock k x raw)
        script += ["%s %s 0 0 0 0\n" % (step, raw)] * n
        at.append(len(script) - 1)
        prev = float(c)
    path = os.path.join(out, "race.t%d.script" % track)
    open(path, "w").write("".join(script))
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", OS_NOMUSIC="1", OS_NOSOUND="1", OS_S2PHYS="1", OS_S2CARS="4", OS_TRACK=str(track),
               OS_S2HOLD="1", OS_S2SCRIPT=os.path.abspath(path), OS_DIFF=open(os.path.join(out, "race.t%d.level.txt" % track)).read().strip(),
               PATH="C:/msys64/ucrt64/bin;" + os.environ["PATH"])
    r = subprocess.run([exe, "-game", "2", "-data", da.game, "-home", os.path.join(out, "home"), "-renderer", "software"], env=env, capture_output=True, text=True, cwd=out)
    rows = [l for l in r.stdout.splitlines() if l.startswith("F ")]
    data = open(os.path.join(out, "race.t%d.bin" % track), "rb").read()
    n = min(len(frames), len(data) // (4 * REC))
    touched = [i for i in range(n) if int(frames[i][3]) > 0 and at[i] >= 0]
    print("%d frames recorded (%d run); the original's solver stopped a step in the frames %s" % (n, len(rows), touched[:20]))
    worst, first_bad = 0.0, None
    for i in range(n):
        if at[i] < 0:
            continue
        cars = [c.split() for c in rows[at[i]].split("|")[1:]]
        d = []
        for k in range(4):
            ox = struct.unpack_from("<i", data, i * 4 * REC + k * REC + 0x4d8)[0]
            oz = struct.unpack_from("<i", data, i * 4 * REC + k * REC + 0x4e0)[0]
            d.append(((ox - int(cars[k][0])) ** 2 + (oz - int(cars[k][2])) ** 2) ** 0.5)
        worst = max(worst, max(d[1:]))
        if first_bad is None and max(d[1:]) > 2.0:
            first_bad = i
        if (touched and 0 <= i - touched[0] < 30 and i % 3 == 0) or i % 100 == 0:
            print("%4d clock %6.2f touch %d | errors of the cars (units): %s" % (i, float(frames[i][0]), int(frames[i][3]), " ".join("%7.1f" % x for x in d)))
    print("the computers leave the original by more than 2 units at frame %s; worst over the recording %.1f units" % (first_bad, worst))


if __name__ == "__main__":
    if sys.argv[1] == "--compare":
        compare(int(sys.argv[2]))
    else:
        track = int(sys.argv[1])
        p, mem = start(track, float(sys.argv[2]) if len(sys.argv) > 2 else 30.0)
        record(mem, track, float(sys.argv[2]) if len(sys.argv) > 2 else 30.0)
        p.kill()
