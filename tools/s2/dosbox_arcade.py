"""The original's arcade pages in DOSBox against the firmware's model of them (docs/screamer2/menus.md "The arcade pages").

Usage: python -I tools/s2/dosbox_arcade.py [extra tracks 0..4] [--check]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2"; --check: the
screenshots of the last run again, without DOSBox)

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (build/dosgame: the data folders are copied, about 0.2 GB, never linked;
CONFIG is a copy whose CHOICE.DAT has the number of extra tracks open: the owner's own is never touched), types its way with scan codes through TEAM SELECTION, the
name and the OPTIONS ROOM to ARCADE, and takes Ctrl+F5 screenshots (320 x 200 palette indices, the raw frame) on the title, the TRACK page, the CAR page and
the TRANSMISSION page. Each is then compared with the frames of tools/ui_arcade_ref.py's model (the page drawn by an independent reading of the
program): the TRACK page whole, the others in the parts the model draws (the original also has the 3D cars: the title's band with the prompt, the CAR
page's title, badge and the left half of the plate, the TRANSMISSION page's title, badge and buttons). DOSBox's window must stay in front while it runs
(about 2 minutes; the keys go to the window in front).
Output: build/check/dosbox/*.png (never committed) and one line per screenshot; exit code 0 if every one equals a frame of the model.

--league (menus row 6): the same run to CHAMPIONSHIP instead (first run: LEAGUE SELECTION in every state of its window, see league() for what it takes now): the desk scene, LEAGUE
SELECTION, WELCOME TO LEAGUE 1, the TRACK page, the car page with its set-ups, the loading screen, one lap of a race (NUMBER OF LAPS 1 on the private CHOICE.DAT) with the player's car
handed to the game's own driver (tools/dosram.py finds the game's data in DOSBox's process; the one thing written to it is that car's controls pointer, as the attract race has it),
the championship's end screen, the LEAGUE REPORT after it and, from it, the TRACK page of the next race with the question LEAVING LEAGUE? that ESC asks there. The screenshots go to
build/check/dosbox_league/ with text files of the game's state (ram_*.txt: the cars' places, laps and times, the points); `--league --check` compares them with the models of
tools/ui_league_ref.py: the selection whole, the welcome page's two lines (its picture is a 3D scene), the end screen's texts and list and the report whole at any of the blink phases,
built from the numbers in the game's memory. The game's keyboard focus is taken for about 5 minutes.

--timeattack [closed|free|race] (menus row 7): the options room's third station, TIME ATTACK. Its TRACK SELECTION (the turning 3D map of the chosen track, flying in and out as the
choice moves; build/check/dosbox_ta/) with all seven tracks open (or, with `closed`, the three of a new save, RIGHT pressed once more than there are tracks), the car page with
its 3 LAPS / FREE RUN choice, and with `race` a race of one lap (the game's lap count is set to 1 once the race runs: time attack's is 3) with the car handed to the game's driver, its
end screen (TOTAL / LAP TIME, then the list); with `free` the free run with the car handed to the driver: the race screen with the five markers' list, the numbers of which
(the text tables at 0x132fc8 and 0x133000) go to ram_*.txt. `--timeattack --check` compares the screenshots with the models (tools/ui_ta_ref.py). Needs the window in front
for about 3 to 6 minutes a run.

--replay (menus row 10): the arcade's race of one lap on England with the player's car handed to the game's driver, the end screen (the first four seconds, then REPLAY | RACE AGAIN | EXIT in thirds), ENTER on
REPLAY and 33 samples of the replay with F11 (the next car), F1 and F9 pressed: the screenshots go to build/check/dosbox_replay/ with replay_<label>.txt (the camera record, the recorder's words and the cars from
the game's memory); `--replay --check` compares the end screen's and the band's text pixels with the models and the camera record with the formulas of firmware/s2cam.c. The keys are the original's. Needs the window
in front for about 7 minutes. Only the DOSBox this script started is keyed and written to (it is found by its configuration file): the owner may have another one running.
"""
import atexit
import ctypes
import ctypes.wintypes as wt
import glob
import itertools
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(root, "tools"))
game = os.environ.get("GAME_DIR", "C:/GOG Games/Screamer 2")
work = os.path.join(root, "build", "check", "dosbox")
private = os.path.join(root, "build", "dosgame")
user32 = ctypes.WinDLL("user32")
ORDER = [1, 3, 4, 5, 6, 2, 7]


class KI(ctypes.Structure):
    _fields_ = [("vk", wt.WORD), ("sc", wt.WORD), ("fl", wt.DWORD), ("time", wt.DWORD), ("ex", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("k", KI), ("pad", ctypes.c_byte * 8)]


keyed_pid = None                                # the one process that is ever sent keys (set where the script starts its DOSBox)


def foreground_pid():
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value


def key(sc, up, ext=False):
    if keyed_pid is not None and foreground_pid() != keyed_pid:     # keys go to the window in front: only ever to the DOSBox this script started
        sys.exit("the window in front is not the DOSBox this script started (pid %d): no key is sent" % keyed_pid)
    i = INPUT(1, KI(0, sc, 8 | (2 if up else 0) | (1 if ext else 0), 0, 0))
    user32.SendInput(1, ctypes.byref(i), ctypes.sizeof(i))


def press(sc, ext=False):
    key(sc, 0, ext)
    time.sleep(0.12)
    key(sc, 1, ext)
    time.sleep(0.12)


def dosbox_pid(marker=None):
    """the DOSBox this script started (the one whose command line has `marker`, its configuration file) or, without a marker, the first one: another DOSBox may be running (the
    owner playing), which must never be sent keys or have its memory written"""
    if marker:
        out = subprocess.run(["powershell", "-NoProfile", "-Command", "Get-CimInstance Win32_Process -Filter \"Name='dosbox.exe'\" | ForEach-Object { '{0} {1}' -f $_.ProcessId, $_.CommandLine }"],
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            if marker.replace("/", "\\").lower() in line.replace("/", "\\").lower():
                return int(line.split()[0])
        return None
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = [x.strip('"') for x in line.split('","')]
        if len(f) > 1 and "dosbox" in f[0].lower():
            return int(f[1])
    return None


def front(pid):
    found = []
    cb = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def each(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            found.append(h)
        return True
    user32.EnumWindows(cb(each), 0)
    for h in found:
        user32.keybd_event(0x12, 0, 0, 0)       # Alt, which lets a background program take the front
        user32.SetForegroundWindow(h)
        user32.keybd_event(0x12, 0, 2, 0)
    time.sleep(0.2)


# ------------------------------------------------------------------------------------------------ the private game folder

def choice_with_extra_tracks(src, dst, extra, team=None, car=None, name=None, league=None, laps=None, cars5=False, detail=None, cursor=None):
    """CONFIG/CHOICE.DAT: word i is kept as (v + i * 0x1050c) ^ i; word 10 (+0x28) is the tracks opened beyond the first three; team (word 0), the
    team's chosen car (the first word of its record at +0xb0 + team * 0x188) and the player's name (+8) as given (--pages3d)"""
    d = open(src, "rb").read()
    w = list(struct.unpack("<%dI" % (len(d) // 4), d))
    dec = [((v ^ i) - i * 0x1050C) & 0xFFFFFFFF for i, v in enumerate(w)]
    dec[10] = extra
    if league is not None:
        dec[11] = league                        # +0x2c: the highest league open
    if laps is not None:
        dec[17] = laps                          # +0x44: the NUMBER OF LAPS row (an index: 0 is one lap)
    if cursor is not None:
        dec[8] = cursor                         # +0x20: where the ARCADE TRACK page's cursor starts (the place in ORDER of the track last chosen)
    if detail is not None:
        dec[16] = detail                        # +0x40: the DETAIL LEVEL row (0 LOW, 1 MEDIUM, 2 HIGH: the wheel smoke and the animated scenery are HIGH's alone)
    dec[0xB0 // 4 + 1] = 1                      # the first team's fifth car won
    if cars5:
        for t in range(4):
            dec[(0xB0 + 0x188 * t) // 4 + 1] = 1     # every team's fifth car
    if team is not None:
        dec[0] = team
    if car is not None:
        dec[(0xB0 + 0x188 * dec[0]) // 4] = car
    if name is not None:
        dec[2] = struct.unpack("<I", name.encode().ljust(4, b"\0"))[0]
    out = [((v + i * 0x1050C) ^ i) & 0xFFFFFFFF for i, v in enumerate(dec)]
    open(dst, "wb").write(struct.pack("<%dI" % len(out), *out) + d[len(out) * 4:])


def drop_junctions():
    """Removes the private folder's junctions as links (rmdir on a junction removes the link only). Called before the folder is touched
    and again when the script ends, so that no junction to the game's own folders is ever left in the repository's build folder: a
    recursive delete there (rm -rf, git worktree remove --force, Remove-Item -Recurse) follows a junction and empties the game's folder"""
    if os.path.isdir(private):
        for name in os.listdir(private):
            p = os.path.join(private, name)
            if os.path.isjunction(p) or os.path.islink(p):
                os.rmdir(p)
        left = [n for n in os.listdir(private) if os.path.isjunction(os.path.join(private, n))]
        assert not left, "a junction could not be removed: " + str(left)


def make_private(extra, **choice):
    drop_junctions()
    shutil.rmtree(private, ignore_errors=True)
    os.makedirs(private)
    for name in os.listdir(game):
        s, d = os.path.join(game, name), os.path.join(private, name)
        if name.upper() in ("DOSBOX", "GAME.GOG", "GAME.DAT", "SCREAMER2.ZIP") or name.lower().endswith((".lnk", ".dll")):
            continue
        if name.upper() == "CONFIG":
            shutil.copytree(s, d)
            choice_with_extra_tracks(os.path.join(s, "CHOICE.DAT"), os.path.join(d, "CHOICE.DAT"), extra, **choice)
        elif os.path.isdir(s):
            shutil.copytree(s, d)                   # (a copy, never a junction: a recursive delete of build/ must not be able to reach the game's own folders)
        else:
            shutil.copy2(s, d)


# ------------------------------------------------------------------------------------------------ the game's memory (--league)

# what a dump keeps (flat addresses of STARTL.EXE's data): the league in hand, the page that is next, the race's record, the four cars' records (a car's record is 0xcc0 bytes from
# 0x119300) and entries (0x2c bytes from 0x130c58), the table of places the end screen makes, the standings the report keeps (12 bytes a car from 0x128a6c)
DUMP = (
    [0x128a10, 0x128a14, 0x128a18, 0x128a1c, 0x128a20, 0x132494, 0x1324c4, 0x130c48, 0x130c4c, 0x130d10, 0x130d48, 0x1289ec]
    + [0x119300 + 0xcc0 * i + o for i in range(4) for o in (0x4c, 0x4a8, 0xc30, 0xc38, 0xc40)]
    + [0x130c58 + 0x2c * i + o for i in range(4) for o in (0, 4, 8)]
    + [0x128a6c + 12 * i + o for i in range(4) for o in (0, 4, 8)]
    + list(range(0x132384, 0x1323c8, 4))
)


# the free run's text tables (0x132fc8 + 11 i: the left column, 0x133000 + 11 i: the right), the best lap's text, the gap message, the lap list timers
FREE_BYTES = [(0x132fc8, 55), (0x133000, 55), (0x132fa4, 12), (0x132f98, 12), (0x13304c, 4), (0x133078, 4), (0x13307c, 4), (0x130d18, 4), (0x130d1c, 20)] + \
             [(0x119300 + 0xc38, 4), (0x119300 + 0xc3c, 4), (0x119300 + 0xc40, 4), (0x119300 + 0xc44, 20), (0x119300 + 0xc58, 20), (0x119300 + 0x4b0, 8), (0x119300 + 0x4a8, 8)]


# the options room's words (menus row 8; STARTL.EXE 0x38e00): the camera record (angles a0 a1 a2, place x y z), the station, the page's next page / leaving flag / fade /
# done flag / sway / the look-at angles (0x1324a8 pitch, 0x1324ac heading), the frame time, the blink clock and the engine's animation clock
ROOM_BLOCKS = [(0x131ea4, 24), (0x132378, 4), (0x132494, 28), (0x133350, 4), (0x4cfd6, 4), (0x1347a0, 4)]
# and what a dump keeps: the cameras' table P_t.MCM was read into (3 records of 0x16c bytes from 0x131f34), the texts' pointers (the title 0x127eec, the five names 0x127ef4), the screen
ROOM_BYTES = [(0x131f34, 3 * 0x16c), (0x127eec, 4), (0x127ef4, 20)]

# time attack's lists (the totals of kind 2 for three laps, the best laps): 300 bytes a track in columns of 15 (times, names, replay numbers, teams, models), the files' track order
TA_LISTS = [(0x12d388 + 0x4b0 * t, 300) for t in range(7)] + [(0x12f32c + 0x1068 + 0x12c * t, 300) for t in range(7)]


class Mem:
    """the running original's data object, found in DOSBox's process by tools/dosram.py (the one thing written to it is the player's car's controls: handed to the game's own
    driver, so that a race finishes by itself, as the attract race does). Flat addresses are STARTL.EXE's."""
    def __init__(self, pid):
        sys.path.insert(0, os.path.join(root, "tools"))
        import dosram
        self.g = dosram.Game(dosram.Proc(pid, write=True), dosram.OBJS2)

    @staticmethod
    def open(pid):
        """the game's memory, or False (said on the screen) if it cannot be found: the run goes on with the keys alone"""
        try:
            return Mem(pid)
        except SystemExit as e:
            print("no access to the game's memory:", e)
            return False

    def word(self, addr):
        return struct.unpack("<i", self.g.flat(addr, 4))[0]

    def wait(self, what, timeout):
        t_end = time.time() + timeout
        while time.time() < t_end:
            if what():
                return True
            time.sleep(0.4)
        return False

    def race(self, folder, mode=2, laps=None, drive=True):
        """the race is set up (the kind of game word 0x130d10 is 2: a championship race; 4 time attack, 8 free run), then 20 s on (the loading, the lights and a few seconds of an
        unmoving car) the player's car's controls pointer (+0xb4) is set to 0: the game's driver takes the car, which drives its lap. laps: the race's laps (0x130d48) set at once
        (time attack's are 3, a lap is long enough to wait for); drive: False leaves the car to the keys"""
        ok = self.wait(lambda: self.word(0x130d10) == mode, 120)
        if laps is not None and ok:
            self.g.poke(0x130d48, struct.pack("<i", laps))
        time.sleep(20)
        ok = ok and (not drive or self.g.poke(0x1193b4, struct.pack("<i", 0)))
        print("the player's car handed to the game's driver:", "yes" if ok else "NO (the race did not start in time)")

    def bytes_at(self, label, folder, ranges):
        """the bytes of the given (address, length) ranges to <folder>/bytes_<label>.txt, one range a line: address, hex"""
        lines = ["%06x %s" % (a, self.g.flat(a, n).hex()) for a, n in ranges]
        open(os.path.join(folder, "bytes_%s.txt" % label), "w").write("\n".join(lines) + "\n")

    def camera(self):
        """the camera record (0xaec bytes at the pointer 0x1192f4), the car it follows (its number, from the pointer at +0xadc) and that car's object, the place the camera uses (car +0x990:
        x, height, z as whole numbers, three angles, the speed at +0x28): (record bytes, car number or -1, object bytes)"""
        cam = self.g.ptr(self.word(0x1192f4), 0xaec)
        car = self.g.to_flat(struct.unpack_from("<I", cam, 0xadc)[0])
        idx = (car - 0x119300) // 0xcc0 if car else -1
        obj = self.g.ptr(struct.unpack("<I", self.g.flat(car + 0x990, 4))[0], 0x40) if car else b""
        return cam, idx, obj

    def replay_state(self, label, folder):
        """the state of a replay (or of the race) for check_replay(): replay_<label>.txt with the kind of game word, the race flags, the recorder's words (0x1330b4 on, 0x1330b8 reading,
        0x1330d0 frames written, 0x1330bc the most, 0x1330c0 the place in a replay), the camera record and its car's object, and each car's flags, laps, place, time, place on the road, speed"""
        cam, idx, obj = self.camera()
        lines = ["mode %d" % self.word(0x130d10), "flags %d" % self.word(0x11f0a8), "rec " + " ".join(str(self.word(a)) for a in (0x1330b4, 0x1330b8, 0x1330d0, 0x1330bc, 0x1330c0)),
                 "follow %d" % idx, "cam " + cam.hex(), "obj " + obj.hex()]
        for i in range(4):
            c, e = 0x119300 + 0xcc0 * i, 0x130c58 + 0x2c * i
            lines.append("car %d %s" % (i, " ".join(str(self.word(a)) for a in (c + 0x4c, c + 0x4a8, c + 0xc30, c + 0xc38, c + 0x4d8, c + 0x4dc, c + 0x4e0, c + 0x500, e, e + 4))))
        open(os.path.join(folder, "replay_%s.txt" % label), "w").write("\n".join(lines) + "\n")

    def poll(self, label, folder, secs, events, shots, times):
        """the options room's variables read as often as the process allows for `secs` seconds (--options): the camera record (angles, place), the station, the page's
        flags and look-at angles, the frame time, the clock and the animation clock. Every change is a row of <folder>/poll_<label>.txt: the time, then the words in hex; the events
        ('s<label>' a screenshot, 'k<hex>' / 'ke<hex>' a key, at their time) are rows of their own, so that a screenshot and a key press can be placed among the frames."""
        rows, last, ev, t0 = [], None, sorted(events), time.time()
        while True:
            now = time.time() - t0
            if now >= secs:
                break
            while ev and ev[0][0] <= now:
                at, what = ev.pop(0)
                if what[0] == "s":
                    times.append(time.time())
                    shots.append(what[1:])
                    for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                        key(sc, up)
                        time.sleep(0.02)
                else:
                    ext = what[1] == "e"
                    sc = int(what[2:] if ext else what[1:], 16)
                    key(sc, 0, ext)
                    time.sleep(0.03)
                    key(sc, 1, ext)
                rows.append("%.4f E %s" % (time.time() - t0, what))
            cur = b"".join(self.g.flat(a, n) for a, n in ROOM_BLOCKS)
            if cur != last:
                rows.append("%.4f %s" % (time.time() - t0, cur.hex()))
                last = cur
        open(os.path.join(folder, "poll_%s.txt" % label), "w").write("\n".join(rows) + "\n")
        print("poll %s: %d rows" % (label, len(rows)))

    def finished(self):
        """the player's car has done its laps (the flag +0x4c bit 1 of its record)"""
        ok = self.wait(lambda: self.word(0x11934c) & 2, 260)
        print("the player's car finished:", "yes" if ok else "NO (not within 260 s)")

    def dump_data(self, label, folder):
        """the whole data object (flat addresses 0x110000 up) to <folder>/data_<label>.bin: what the game's records hold at this moment"""
        lo, hi = self.g.objs[3]
        open(os.path.join(folder, "data_%s.bin" % label), "wb").write(self.g.flat(lo, hi - lo))

    def find_ints(self, label, folder, triple, tol=15, span=0x3000000):
        """where in the guest's memory (the data object's linear base up to span bytes above it: the heap too) three whole numbers in a row are within tol of
        `triple`: the hits and 0x300 bytes round each go to <folder>/find_<label>.txt (the pages' objects live there, not in the data object)"""
        import numpy as np
        lin0 = self.g.obj[3][1]
        out = []
        for off in range(0, span, 1 << 22):
            chunk = self.g.ptr(lin0 + off, (1 << 22) + 16)
            if not chunk or len(chunk) < 64:
                continue
            w = np.frombuffer(chunk[:len(chunk) & ~3], dtype="<i4")
            hit = np.nonzero((abs(w[:-2] - triple[0]) <= tol) & (abs(w[1:-1] - triple[1]) <= tol) & (abs(w[2:] - triple[2]) <= tol))[0]
            for i in hit[:8]:
                a = lin0 + off + 4 * int(i)
                out.append("hit at linear 0x%x: %s\n%s" % (a, list(map(int, w[i:i + 3])), self.g.ptr(a - 0x180, 0x300).hex()))
        open(os.path.join(folder, "find_%s.txt" % label), "w").write("\n".join(out) + "\n")
        print("find %s: %d hits" % (label, len(out)))

    def dump(self, label, folder):
        """the words of DUMP, <address> <value> a line, to <folder>/ram_<label>.txt"""
        lines = ["%06x %d" % (a, self.word(a)) for a in DUMP]
        open(os.path.join(folder, "ram_%s.txt" % label), "w").write("\n".join(lines) + "\n")


# ------------------------------------------------------------------------------------------------ the session

def session(steps):
    """steps: w<sec> wait, k<hex> a key (ke<hex> an extended one), r<n>,<dt> n screenshots dt seconds apart, L<label> names what follows, q quit;
    K<hex> / Ke<hex> a key without waiting after it and R<n>,<dt> screenshots without bringing the window to the front first: the frames in
    the first half second after a key (an easing in flight). Returns [(png, label)]"""
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    conf = os.path.join(work, "run.conf")
    open(conf, "w").write('[sdl]\nfullscreen=false\nwindowresolution=original\n[dosbox]\ncaptures=%s\n[autoexec]\n@echo off\nmount C "%s"\n'
                          'imgmount d "%s/GAME.DAT" -t iso\nc:\nSTARTL.EXE\nexit\n' % (work, private, game))
    p = subprocess.Popen([game + "/DOSBOX/dosbox.exe", "-conf", game + "/dosbox_screamer2.conf", "-conf", conf, "-noconsole"], cwd=work)
    atexit.register(p.kill)                     # (our own DOSBox goes with the script, also when a step fails)
    time.sleep(2)
    pid = dosbox_pid(conf) or p.pid             # (only the DOSBox this script started is keyed: found by its configuration file; if it is not there the script stops)
    if p.poll() is not None:
        sys.exit("the DOSBox this script started is not running: no key is sent")
    global keyed_pid
    keyed_pid = pid
    label, shots, times, mem = "", [], [], None
    for st in steps.split():
        if st[0] == "w":
            time.sleep(float(st[1:]))
        elif st[0] == "M":                      # the game's memory found (it takes a while: before the first J)
            t_mem = time.time()
            mem = Mem.open(pid) if mem is None else mem
            print("the game's memory: %s after %.1f s" % ("found" if mem else "not found", time.time() - t_mem))
        elif st[0] == "X":                      # --league: Xrace hands the player's car to the game's driver once the race runs, Xfin waits until the car has finished;
            mem = Mem.open(pid) if mem is None else mem      # --timeattack: Xta (mode 4, one lap) and Xfree (mode 8) the same; Xkeys only waits for the race to run
            if mem:
                if st[1:] == "race":
                    mem.race(work)
                elif st[1:] == "ta":
                    mem.race(work, 4, 1)
                elif st[1:] == "free":
                    mem.race(work, 8)
                elif st[1:] == "arc":                       # --replay: the arcade's race (mode 1), one lap
                    mem.race(work, 1)
                else:
                    mem.finished()
        elif st[0] == "P":                      # --replay: P<label> the camera record, the recorder's words and the cars to replay_<label>.txt
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.replay_state(st[1:], work)
        elif st[0] == "Z":                      # --options: Z<label> the cameras' table and the texts' pointers to bytes_<label>.txt
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.bytes_at(st[1:], work, ROOM_BYTES)
        elif st[0] == "T":                      # --timeattack: T<label> time attack's lists to bytes_<label>.txt
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.bytes_at(st[1:], work, TA_LISTS)
        elif st[0] == "B":                      # --timeattack: Bfree<label> the bytes of the free run's text tables to bytes_<label>.txt
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.bytes_at(st[1:], work, FREE_BYTES)
        elif st[0] == "F":                      # F<label>:<x>,<y>,<z>: where whole numbers like these are in the guest's memory (find_<label>.txt)
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                lab, nums = st[1:].split(":")
                if nums == "car":               # the player's car (the record at 0x119300): its place x, y (floats +0x4d8, +0x4e0) and height (+0x4dc) wherever the game keeps them
                    rec = mem.g.flat(0x119300, 0x500)
                    x, y, h = struct.unpack_from("<f", rec, 0x4d8)[0], struct.unpack_from("<f", rec, 0x4e0)[0], struct.unpack_from("<i", rec, 0x4dc)[0]
                    print("the player's car at", x, y, h)
                    mem.find_ints(lab + "_xyh", work, (int(x), int(y), h), 60)
                    mem.find_ints(lab + "_xhy", work, (int(x), h, int(y)), 60)
                else:
                    mem.find_ints(lab, work, tuple(int(v) for v in nums.split(",")))
        elif st[0] == "Y":                      # the game's data object into data_<label>.bin
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.dump_data(st[1:], work)
        elif st[0] == "D":                      # the game's league state into a text file
            mem = Mem.open(pid) if mem is None else mem
            if mem:
                mem.dump(st[1:], work)
        elif st[0] == "W":                      # --options: W<label>:<seconds>[:<event>@<t> ...] polls the options room's variables for that long (mem.poll)
            mem = Mem.open(pid) if mem is None else mem
            front(pid)
            if mem:
                f = st[1:].split(":")
                events = []
                for e in f[2:]:
                    what, at = e.split("@")
                    events.append((float(at), what))
                mem.poll(f[0], work, float(f[1]), events, shots, times)
        elif st[0] == "k":
            front(pid)
            ext = st[1] == "e"
            press(int(st[2:] if ext else st[1:], 16), ext)
        elif st[0] == "K":
            ext = st[1] == "e"
            key(int(st[2:] if ext else st[1:], 16), 0, ext)
            time.sleep(0.03)
            key(int(st[2:] if ext else st[1:], 16), 1, ext)
        elif st[0] in "rR":
            n, dt = st[1:].split(",")
            for _ in range(int(n)):
                if st[0] == "r":
                    front(pid)
                times.append(time.time())
                for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                    key(sc, up)
                    time.sleep(0.04 if st[0] == "r" else 0.02)
                shots.append(label)
                time.sleep(float(dt))
        elif st[0] == "L":
            label = st[1:]
        elif st[0] == "q":
            p.kill()
    time.sleep(0.5)
    files = sorted(glob.glob(os.path.join(work, "*.png")))
    if len(files) != len(shots):
        # DOSBox drops a capture that comes too quickly after the last (the fast R requests): each file takes the label of the latest request before its time
        print(f"{len(files)} screenshots for {len(shots)} requests: the files are labelled by the time of the request before them")
        got = []
        for f in files:
            t = os.path.getmtime(f)
            before = [i for i, r in enumerate(times) if r <= t + 0.05 and t - r < 1.5]
            got.append(shots[before[-1]] if before else "?")
        shots = got
    open(os.path.join(work, "labels.txt"), "w").write("\n".join(shots) + "\n")
    return list(zip(files, shots))


def read_png(path):
    """a palette-indexed 320 x 200 PNG: the indices"""
    d = open(path, "rb").read()
    p, idat = 8, b""
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", d[p + 8:p + 18])
        elif kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    assert (w, h, bd, ct) == (320, 200, 8, 3), "not a raw 320 x 200 capture"
    raw, rows, prev = zlib.decompress(idat), [], bytearray(w)
    for y in range(h):
        f, line = raw[y * (w + 1)], bytearray(raw[y * (w + 1) + 1:(y + 1) * (w + 1)])
        for i in range(w):
            a, b, c = line[i - 1] if i else 0, prev[i], prev[i - 1] if i else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return b"".join(rows)


# ------------------------------------------------------------------------------------------------ the model's frames

def model():
    import ui_arcade_ref
    import ui_reference as ur
    graf = ur.container(game, "GFX/GRAF1.DAT")
    spr = ur.sprites(ur.datfile.extract(graf, "MENUL.SPR"))
    fnt = ur.fonts(ur.datfile.extract(graf, "MENUL.FNT"))
    helpers = {"bkk": ur.bkk, "container": ur.container, "datfile": ur.datfile, "Canvas": ur.Canvas}
    return ui_arcade_ref.Ref(game, graf, spr, fnt, work, helpers)


def diff(a, b, box=(0, 0, 320, 200)):
    x0, y0, x1, y1 = box
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if a[y * 320 + x] != b[y * 320 + x])


def check(shots, extra, ref):
    start = {"track": 1, "team": 1, "car": 2, "manual": 0}
    n = 3 + extra
    bad = 0
    pic = ("ARCADE_L.BKK", "BKG_1L.BKK", "BKG_2L.BKK", "BKG_2L.BKK")

    def frames(page, st, nopen, keys, ticks, force=None):
        """the model's frames at the given ticks after the keys (a dict tick: key); force: a track page's (frame, arrows) shown or not"""
        s = dict(st, clicks=[])
        m = (ref.title(s), ref.track(s, nopen), ref.car(s, 1), ref.car(s, 1, True))[page]       # (the CAR page and its second state, the TRANSMISSION choice)
        m.force = force
        out = {}
        for t in range(max(ticks) + 1):
            m.key(t, {keys[t]} if t in keys else set())
            c = ref.canvas(pic[page])
            m.draw(t, c)
            if t in ticks:
                out[t] = bytes(c.px)
        return out

    track_cands = {}                        # every settled place with its blink states, and the 40 frames of every move
    for pos in range(n):
        st = dict(start, track=ORDER[pos])
        for fr, ar in itertools.product((False, True), repeat=2):
            tag = f"frame {'on' if fr else 'off'} arrows {'on' if ar else 'off'}"
            track_cands[f"place {pos} {tag}"] = frames(1, st, n, {}, [60], (fr, ar))[60]
            if pos + 1 < n:
                for t, v in frames(1, st, n, {5: "R"}, range(5, 46), (fr, ar)).items():
                    track_cands[f"move {pos}->{pos + 1} tick {t - 5} {tag}"] = v
    title = {k: frames(0, start, n, {}, [t])[t] for k, t in (("prompt on", 0), ("prompt off", 40))}
    car = frames(2, start, n, {}, [0])[0]
    trans = {(m, lit): frames(3, dict(start, manual=m), n, {}, [2 if lit else 20])[2 if lit else 20] for m in (0, 1) for lit in (True, False)}
    rowsets = None
    for png, label in shots:
        idx = read_png(png)
        name = os.path.basename(png)
        if label == "title":
            d = {k: diff(idx, v, (0, 176, 320, 200)) for k, v in title.items()}
        elif label == "track":
            d = {k: (0 if idx == v else diff(idx, v)) for k, v in track_cands.items()}
        elif label == "car":
            d = {"title, badge, plate": diff(idx, car, (28, 15, 213, 44)) + diff(idx, car, (226, 1, 290, 50)) + diff(idx, car, (18, 155, 160, 182))}
        else:
            d = {f"manual {m} lit {int(lit)}": diff(idx, v, (28, 15, 213, 44)) + diff(idx, v, (226, 1, 290, 50)) + diff(idx, v, (0, 150, 320, 190))
                 for (m, lit), v in trans.items()}
        best = min(d, key=d.get)
        ok = d[best] == 0 or (label == "trans" and d[best] < 150)       # (a wheel of the 3D car may reach into the buttons' band)
        verdict = "equal" if d[best] == 0 else "differs by " + str(d[best])
        if not ok and label == "track":
            # the original draws on the screen it shows, so a screenshot can catch a frame half drawn: rows of one tick of the easing, rows of the next
            if rowsets is None:
                rowsets = [{v[y * 320:(y + 1) * 320] for v in track_cands.values()} for y in range(200)]
            torn = sum(1 for y in range(200) if idx[y * 320:(y + 1) * 320] not in rowsets[y])
            if torn == 0:
                ok, verdict = True, "torn: every row equals a row of the model's frames"
            else:
                verdict += f", {torn} rows of 200 are in no frame of the model"
        bad += not ok
        print(f"{name} {label:6s} {verdict:20s} {best}")
    return bad


def pages3d():
    """the screenshots of the 3D pages for tools/pages3d.sh (build/check/dosbox3d): team 1 (CONDOR) with the name MIL and its second car chosen"""
    global work
    work = os.path.join(root, "build", "check", "dosbox3d")
    atexit.register(drop_junctions)
    make_private(0, team=0, car=1, name="MIL")
    # TEAM SELECTION: the badges turning (the chosen one at half a turn a second, the others at an eighth), then a move to the right and back
    # (the badges glide 10 % of the way a frame), ENTER YOUR NAME: the badges fly off and the letter tyres in, the wheel turns a letter (right, left), the
    # cursor moves down; ENTER leaves for the options room, whose ARCADE row leads over the title and TRACK pages to the CAR page: the carousel turning, one move
    steps = ("w12 k1c w1.2 Lteam r12,0.25 Ke4d R5,0 w0.8 Lteam_move r3,0.1 w2 Lteam_after r2,0.5 Ke4b R5,0 w2.5 "
             "k1c R6,0 w0.6 Lname r5,0.15 w1.5 Lname_rest r4,0.3 Ke4d R5,0 w0.6 Lname_move r4,0.2 Ke50 R3,0 w0.6 Lname_cursor r3,0.3 "
             "k1c w6 k1c w6 k1c w5 k1c w5 Lcar r10,0.3 Ke4d R5,0 w1.5 Lcar_move r6,0.2 q")
    shots = session(steps)
    print(f"{len(shots)} screenshots in {work}")


# ------------------------------------------------------------------------------------------------ the championship's pages (menus row 6)

LEAGUE_TICKS = [0, 16, 17, 32, 48]              # frames of a model that cover every blink phase (clock = 2 x tick: the frame and the arrows 0x3f / 0x7f, the question's 0x3f)


def cars(team, race, moves=False):
    """--cars <team 1..4> [race]: the CAR page of the team with all five cars open (screenshots of the carousel at rest and the game's data object, build/check/dosbox_cars),
    with `race` the arcade race on track 1 (the grid and the first seconds: screenshots and the data object)"""
    global work
    work = os.path.join(root, "build", "check", "dosbox_cars")
    atexit.register(drop_junctions)
    make_private(0, team=team - 1, car=1, name="MIL", cars5=True)
    boot = "w12 k1c w6 k1c w6 k1c w6 k1c w9 k1c w4 "
    carousel = "".join("Ke4d R3,0 w2.5 Lcar%d r3,0.4 " % (k + 2) for k in range(4)) if moves else ""    # moves: the carousel one place on four times, so that every car of the team has been the chosen one
    steps = boot + "k1c w5 Lcar r%d,0.4 M Ycar " % (3 if moves else 12) + carousel + ("k1c w1.5 k1c w20 Lgrid r3,0.5 Ygrid Fpl:car w5 Lrace r3,1 Yrace q" if race else "q")
    shots = session(steps)
    print(f"{len(shots)} screenshots in {work}")


def league():
    """the championship against the running original (see the header): CHAMPIONSHIP in the options room, the desk scene, LEAGUE SELECTION, WELCOME TO LEAGUE 1, the TRACK page, the car
    page with its set-ups, the loading screen and a race of one lap (the NUMBER OF LAPS setting of the private CHOICE.DAT; the player's car is handed to the game's own driver once the
    lights are out), its end screen (the first four seconds, then the list of the cars that have done their laps) and the LEAGUE REPORT, and from it the TRACK page of the next race with
    the question LEAVING LEAGUE? that ESC asks there. League 1 only is open on the private CHOICE.DAT; the player is team 1 named MIL; the keys are the original's (ENTER 0x1c, ESC
    0x01, the arrows extended)"""
    global work
    work = os.path.join(root, "build", "check", "dosbox_league")
    atexit.register(drop_junctions)
    make_private(0, team=0, car=1, name="MIL", league=0, laps=0)
    steps = (
        # the game's memory is looked for while the attract sequence plays (87 s; a page that waits 30 s goes back to it), then ENTER: TEAM SELECTION, the name, the options room:
        # RIGHT to the second station, CHAMPIONSHIP, ENTER: the desk scene (a 3D room that leads on by itself after about 3 s)
        "w3 M k1c w6 k1c w6 k1c w6 ke4d w4 k1c w6 "
        # LEAGUE SELECTION: the window over the leagues (the second one is not open), every state three or four times (the blinking), ENTER on a league that is not open does
        # nothing; then ENTER: WELCOME TO LEAGUE 1; ENTER: the TRACK page; ENTER: the car page; ENTER: the set-ups; ENTER: the loading screen, then the race
        "Lsel_0_0 r4,0.55 ke4d w0.8 Lsel_1_0 r4,0.55 ke4d w0.8 Lsel_2_1 r4,0.55 k1c w1.5 Lsel_2_1_enter r2,0.5 ke4d w0.8 Lsel_3_2 r4,0.55 "
        "ke4b w0.8 Lsel_2_2 r3,0.55 ke4b w0.8 Lsel_1_1 r3,0.55 ke4b w0.8 Lsel_0_0 r3,0.55 "
        "k1c w3 Lwelcome r3,0.6 k1c w7 Ltrackmap r2,0.6 k1c w6 Lcarpage r2,0.6 k1c w2 Lcarsetup r2,0.6 k1c w2 Lloading r2,1.0 "
        # the race: its car to the game's driver, the lap; the end screen's first four seconds, then its list, the numbers of the cars to a file; RIGHT (CONTINUE: REPLAY is first), ENTER
        "Xrace Xfin w1.2 Lend_cong r2,0.5 w1.8 Lend_box r3,0.6 Dend ke4d w0.6 k1c w4 Lrep_a r4,0.55 Drep_a "
        # the report's ENTER with races left: the TRACK page of race 2, ESC asks LEAVING LEAGUE (NO first, LEFT YES, RIGHT NO)
        "k1c w8 Ltrackmap2 r2,0.6 k01 w1.2 Lprompt r4,0.45 ke4b w0.6 Lprompt_yes r3,0.45 ke4d w0.6 Lprompt_no r3,0.45 q"
    )
    shots = session(steps)
    print(f"{len(shots)} screenshots in {work}")


# ------------------------------------------------------------------------------------------------ the options room (menus row 8)

def options(which):
    """the options room against the running original (see the header): team 1 (CONDOR) named MIL on a private CHOICE.DAT. `which`: "" the room's five stations (the camera
    flying in, each station settled, each move), "pages" ENTER on each station (the fade out and the page it leads to) and ESC (back to TEAM SELECTION). Every step that samples
    the game's variables (W) writes poll_<label>.txt with each change of the camera record; the keys are the original's (ENTER 0x1c, ESC 0x01, the arrows extended)."""
    global work
    team = int(which[4:]) if which.startswith("team") else 1
    work = os.path.join(root, "build", "check", "dosbox_room" + ("_t%d" % team if team > 1 else "_pages" if which == "pages" else ""))
    atexit.register(drop_junctions)
    make_private(0, team=team - 1, car=1, name="MIL")
    boot = "w3 M k1c w6 k1c w6 "                # the attract sequence plays while the game's memory is found; ENTER: TEAM SELECTION, ENTER: ENTER YOUR NAME, then the third ENTER:
    if which.startswith("team"):                # another team's room: each station settled, RIGHT to the next (the team is the private CHOICE.DAT's)
        steps = boot + "k1c w4 "
        for st in range(5):
            steps += "Wst%d:2.5:sst%da@1:sst%db@2 " % (st, st, st)
            if st < 4:
                steps += "ke4d w4 "
        steps += "q"
    elif which == "":
        steps = boot + "k1c Wfly:9:sfly0@0.3:sfly1@0.9:sfly2@1.5:sfly3@2.2:sfly4@3.2:sfly5@5:sfly6@7.5 Zroom "
        for st in range(5):
            steps += "Wst%d:3:sst%da@1:sst%db@2.2 " % (st, st, st)
            if st < 4:                          # RIGHT to the next station: shots in flight and at rest
                steps += "Wmv%d:5:ke4d@0.3:smv%da@0.55:smv%db@1.05:smv%dc@1.6:smv%dd@2.3:smv%de@3.4 " % ((st,) * 6)
        steps += "q"
    else:
        # ENTER on each station (the fade out: the camera drifts towards the aim point, the frames' brightness; then the page it leads to, whose own ESC comes back to the room: the
        # title of ARCADE, LEAGUE SELECTION after the desk scene, TRACK SELECTION, the MULTIPLAYER page, GAME SETTINGS), the fly-in again on the return, RIGHT to the next station;
        # at the last one ESC: TEAM SELECTION
        steps = boot + "k1c Wfly:5 Zroom "
        wait = (4.5, 8.5, 4.5, 4.5, 4.5)       # the page's time to come up (the championship's desk scene first)
        for st in range(5):
            steps += "Wp%d_enter:%.1f:sp%d_room@0.2:k1c@0.7:sp%d_fade@1.0:sp%d_page@%.1f " % (st, wait[st] + 1.0, st, st, st, wait[st])
            steps += "k01 Wp%d_back:5:sp%d_back0@0.5:sp%d_back1@3 " % (st, st, st)
            if st < 4:
                steps += "ke4d w3.5 "
        steps += "Wesc:4:k01@0.3:sesc_team@2 q"
    shots = session(steps)
    print(f"{len(shots)} screenshots in {work}")


# ------------------------------------------------------------------------------------------------ time attack (menus row 7)

def timeattack(which):
    """time attack against the running original (see the header): `which` is "" (all seven tracks open: TRACK SELECTION in every state, then the car page), "closed" (a new save: three
    tracks), "race" (England, one lap, the end screen) or "free" (the free run). The keys are the original's (ENTER 0x1c, ESC 0x01, the arrows extended)."""
    global work
    work = os.path.join(root, "build", "check", "dosbox_ta" + ("_" + which if which else ""))
    atexit.register(drop_junctions)
    make_private(0 if which == "closed" else 4, team=0, car=1, name="MIL")
    # the attract sequence plays while the game's memory is looked for; ENTER leaves it for TEAM SELECTION, the name, the options room: RIGHT twice to the third station, TIME ATTACK
    boot = "w3 M k1c w6 k1c w6 k1c w6 ke4d w4 ke4d w4 k1c w7 "
    tracks = ""
    if which == "":
        # TRACK SELECTION: the first track settled (the map turns once in 8.2 s: shots spread over it), then RIGHT to each next one (the old map leaves and the new comes in
        # within a quarter second: shots right after the key), at the end one RIGHT more than there are tracks
        tracks = "Lta_0 r4,0.9 "
        for p in range(1, 7):
            tracks += "Ke4d R4,0.04 w0.5 Lta_%d r3,1.1 " % p
        tracks += "ke4d w1 Lta_last r2,0.9 "
        tracks += "ke4b w1.2 Lta_back r2,0.6 ke4b w0.6 ke4b w0.6 ke4b w0.6 ke4b w0.6 ke4b w0.6 ke4b w0.6 ke4b w1.2 Lta_first r2,0.6 "
        tracks += "k1c w7 Lta_car0 r3,0.6 k1c w1.5 Lta_car1 r3,0.6 k1c w1.5 Lta_mode r4,0.5 ke4d w0.8 Lta_mode_free r4,0.45 q"
    elif which == "closed":
        tracks = "Lta_0 r3,0.9 ke4d w1.2 Lta_1 r3,0.9 ke4d w1.2 Lta_2 r3,0.9 ke4d w1 Lta_2_more r3,0.9 ke4d w1 Lta_2_more2 r2,0.9 q"
    elif which == "race":
        tracks = ("Lta_0 r2,0.5 k1c w7 Lta_car0 r1,0.5 k1c w1.5 Lta_car1 r1,0.5 k1c w1.5 Lta_mode r1,0.5 k1c w2 Lta_loading r2,1.0 "
                  "Xta Lta_race r3,1.5 Xfin w1.2 Lta_end_a r2,0.5 w1.8 Lta_end_b r2,0.5 w1 Dend Tend w2 Lta_end_list r4,0.6 q")
    elif which == "free":
        tracks = ("Lta_0 r1,0.5 k1c w7 k1c w1.5 k1c w1.5 ke4d w0.8 Lta_mode_free r2,0.5 k1c w2 Lta_loading r2,1.0 "
                  "Xfree Lfree_1 r2,0.6 Bfree_1 w6 Lfree_2 r2,0.6 Bfree_2 w9 Lfree_3 r2,0.6 Bfree_3 w9 Lfree_4 r2,0.6 Bfree_4 w9 Lfree_5 r2,0.6 Bfree_5 w9 Lfree_6 r2,0.6 Bfree_6 "
                  "w9 Lfree_7 r2,0.6 Bfree_7 w9 Lfree_8 r2,0.6 Bfree_8 w9 Lfree_9 r2,0.6 Bfree_9 w9 Lfree_10 r2,0.6 Bfree_10 w9 Lfree_11 r2,0.6 Bfree_11 q")
    shots = session(boot + tracks)
    print(f"{len(shots)} screenshots in {work}")


def read_bytes_dump(label):
    """a dump of Mem.bytes_at(): {address: bytes}"""
    out = {}
    for line in open(os.path.join(work, "bytes_%s.txt" % label)):
        a, h = line.split()
        out[int(a, 16)] = bytes.fromhex(h)
    return out


def check_free(shots):
    """the free run's race screen against the model of tools/hud_reference.py: the text tables the game kept at the moment (bytes_free_<k>.txt, the strings of 0x132fc8 and 0x133000 as the game
    has them, the message of 0x132f98, its timer) drawn by the model's text routine at the places read from the code; the pixels the model draws are compared with the screenshots' (the
    scene is under them). The live row (the running time of the next marker) and the lap's clock change while the dump and the screenshots are a second apart: not compared."""
    import hud_reference
    hud = hud_reference.Hud(game)
    T = hud.text
    tot_n = tot_same = 0
    done = set()
    for png, label in shots:
        if not label.startswith("free_"):
            continue
        try:
            d = read_bytes_dump(label)
        except FileNotFoundError:
            continue
        idx = read_png(png)
        lt = [d[0x132fc8][11 * i:11 * i + 11].split(b"\0")[0].decode("latin-1") for i in range(5)]
        rt = [d[0x133000][11 * i:11 * i + 11].split(b"\0")[0].decode("latin-1") for i in range(5)]
        timer = struct.unpack("<f", d[0x13304c])[0]
        cp = struct.unpack("<i", d[0x119300 + 0x4b0][4:8])[0]
        live = cp % 5 if timer <= 0.5 else -1                  # (the list is frozen while it is held)
        c = hud_reference.Canvas()
        hud.put_text(c, 3, 3, -1, 8, 16, T[0])
        hud.put_text(c, 3, 3, -1, 0x10e, 16, T[2])
        for k in range(5):
            hud.put_text(c, 3, 3, -1, 8, 0x38 + 8 * k, chr(65 + k))
            hud.put_text(c, 3, 3, -1, 0x105, 0x38 + 8 * k, chr(65 + k))
            if k != live:
                hud.put_text(c, 3, 1, -1, 0x12, 0x38 + 8 * k, lt[k])
            hud.put_text(c, 3, 1, -1, 0x10a, 0x38 + 8 * k, rt[k])
        n = same = 0
        for i in range(64000):
            if c.px[i]:
                n += 1
                same += idx[i] == c.px[i]
        tot_n += n
        tot_same += same
        print("%s %-9s the free run's text tables (labels, columns%s): %d of %d drawn pixels equal" % (os.path.basename(png), label, ", live row %d left out" % live if live >= 0 else "", same, n))
    print("free run: %d of %d pixels equal" % (tot_same, tot_n))
    return 0 if tot_same == tot_n and tot_n else 1


def league_models():
    """the models of tools/ui_league_ref.py (the pages) and its End class (the championship's end screen)"""
    sys.path.insert(0, os.path.join(root, "tools"))
    import ui_league_ref
    import ui_reference as ur
    graf = ur.container(game, "GFX/GRAF1.DAT")
    spr = ur.sprites(ur.datfile.extract(graf, "MENUL.SPR"))
    fnt = ur.fonts(ur.datfile.extract(graf, "MENUL.FNT"))
    helpers = {"bkk": ur.bkk, "container": ur.container, "datfile": ur.datfile, "Canvas": ur.Canvas, "sprites": ur.sprites}
    ref = ui_league_ref.Ref(game, graf, spr, fnt, work, helpers)
    ref.end = ui_league_ref.End(game, ur.container(game, "GFX/GAME1.DAT"), spr, fnt, helpers)
    return ref


def read_ram(label):
    """a dump of Mem.dump(): {flat address: word}"""
    ram = {}
    for line in open(os.path.join(work, "ram_%s.txt" % label)):
        a, v = line.split()
        ram[int(a, 16)] = int(v)
    return ram


def float_of(word):
    return struct.unpack("<f", struct.pack("<i", word))[0]


def string_of(word):
    return struct.pack("<i", word).split(b"\0")[0].decode("latin-1")


def league_cars(ram):
    """the four cars as the end screen lists them: (name, team 1 .. 4, car 1 .. 5, place, has done its laps, time)"""
    cars = []
    for i in range(4):
        e, c = 0x130c58 + 0x2c * i, 0x119300 + 0xcc0 * i
        cars.append((string_of(ram[e + 8]), ram[e] + 1, ram[e + 4] + 1, ram[c + 0xc30], int(ram[c + 0x4a8] >= ram[0x130d48]), float_of(ram[c + 0xc38])))
    return cars


def league_candidates(ref, label):
    """the model's frames for a screenshot's label: [(frame, drawn, whole)]: the frame as indices; whole: the picture is 2D and every pixel is compared, else drawn marks the pixels
    the model draws over a picture the model does not have (a 3D scene), the only ones compared"""
    parts = label.split("_")
    out = []
    if parts[0] == "sel":
        cursor, first = int(parts[1]), int(parts[2])
        lopen = 1 if parts[-1] == "o1" else 0
        for tick in LEAGUE_TICKS:
            st = dict(league=cursor, lopen=lopen, clicks=[])
            m = ref.select(st)
            m.first = first
            a, dk, b = m.draw(tick)
            out.append((compose(ref, a, dk, b, "BKG_3L.BKK"), None, True))
    elif parts[0] == "welcome":
        m = ref.welcome(dict(league=0, clicks=[]))
        a, dk, b = m.draw(0)
        out.append((compose(ref, a, dk, b, m.pic), bytes(1 if x else 0 for x in b.px), False))
    elif parts[0] == "rep":
        ram = read_ram("rep_a")
        points = [ram[0x128a74 + 12 * i] for i in range(4)]
        for tick in LEAGUE_TICKS:
            st = dict(league=ram[0x128a10], done=ram[0x128a1c], points=points, team=ram[0x1289ec] + 1, name=string_of(ram[0x130c60]), lopen=0, topen=0, car5=0, clicks=[])
            m = ref.report(st)
            a, dk, b = m.draw(tick)
            out.append((compose(ref, a, dk, b, m.pic), None, True))
    elif parts[0] == "end":
        ram = read_ram("end")
        cars = league_cars(ram)
        track, place, ptime = ram[0x130c4c] + 1, ram[0x119300 + 0xc30], float_of(ram[0x119300 + 0xc38])
        for clock, blink in ((0, 0x20),) if parts[1] == "cong" else ((400, 0x00), (400, 0x20)):
            c, dk = ref.end.draw(track, place, ptime, cars, clock, blink)
            drawn = bytearray(1 if x else 0 for x in c.px)
            for y in range(170, 200):                               # (the original's choices under the list: REPLAY and CONTINUE, two halves)
                drawn[y * 320:(y + 1) * 320] = bytes(320)
            out.append((bytes(c.px), bytes(drawn), False))
    return out


def compose(ref, a, dk, b, pic):
    """a model frame as indices: the first layer, the darkened box (with the table of the picture's palette), the second layer over it"""
    table = dark_table(ref.pics[pic][0]) if any(dk) else None
    px = bytearray(a.px)
    for i in range(64000):
        if dk[i]:
            px[i] = table[px[i]]
        if b.px[i]:
            px[i] = b.px[i]
    return bytes(px)


def dark_table(pal):
    """the original's darkening: for each colour the entry nearest to 0.67 of it (the first of equals); where that is the colour itself, the entry nearest to 0.67 x 0.7 of it (a
    search written here again, in double precision: the firmware's table is checked against the same rule by tools/host/ui_screen.c)"""
    def near(c, k):
        want = [(c >> s & 255) * k for s in (16, 8, 0)]
        return min(range(256), key=lambda q: sum(((pal[q] >> s & 255) - w) ** 2 for s, w in zip((16, 8, 0), want)))
    table = []
    for i in range(256):
        q = near(pal[i], 0.67)
        table.append(near(pal[i], 0.67 * 0.7) if q == i else q)
    return table


def check_league(shots):
    ref = league_models()
    bad = 0
    for png, label in shots:
        name = os.path.basename(png)
        try:
            cands = league_candidates(ref, label)
        except (FileNotFoundError, KeyError) as e:
            print(f"{name} {label:18s} no state of the game for it ({e.__class__.__name__}: {e})")
            bad += 1
            continue
        if not cands:
            print(f"{name} {label:18s} (looked at, not compared)")
            continue
        idx = read_png(png)
        best, best_d = None, None
        for k, (frame, drawn, whole) in enumerate(cands):
            d = diff(idx, frame) if whole else sum(1 for i in range(64000) if drawn[i] and idx[i] != frame[i])
            if best_d is None or d < best_d:
                best, best_d = k, d
        ok = best_d == 0
        bad += not ok
        print(f"{name} {label:18s} {'equal at candidate %d' % best if ok else 'differs by %d (nearest candidate %d)' % (best_d, best)}")
    return bad


# ------------------------------------------------------------------------------------------------ the replay (menus row 10)

def replay():
    """the replay against the running original (see the header): the arcade's race of one lap on England with the player's car handed to the game's driver (the recorder then keeps what
    a car without controls has: the replay's car does not drive the race it was recorded in, so the replay is looked at for what it shows and does, not for the race it replays), the
    camera record and the cars at the finish, the end screen's choices (REPLAY first, in thirds), ENTER on REPLAY, the replay: the trackside cameras, the band, F11 the next car, F4 the
    chase camera and F1 the car's own view (the band's words), and the end of it. The keys are the original's (ENTER 0x1c, ESC 0x01, F1 0x3b, F4 0x3e, F11 0x57, arrows extended)."""
    global work
    work = os.path.join(root, "build", "check", "dosbox_replay")
    atexit.register(drop_junctions)
    make_private(0, team=0, car=1, name="MIL", laps=0)
    steps = (
        # the attract sequence plays while the game's memory is looked for; ENTER: TEAM SELECTION, the name, the options room (ARCADE), the title, the TRACK page (England), the CAR page,
        # the TRANSMISSION page, then the loading screen and the race
        "w3 M k1c w6 k1c w6 k1c w6 k1c w9 k1c w4 k1c w5 k1c w2 k1c w3 "
        # the race: the car to the game's driver after 20 s, the lap; the camera at the finish (the cut to the trackside cameras), the end screen's first four seconds, its choices
        "Xarc Pgrid Xfin w0.4 Pfin Lend_cong r2,0.5 w1.8 Lend_box r1,0.5 Dend Lend_c0 r4,0.45 ke4d w0.4 Lend_c1 r3,0.45 ke4d w0.4 Lend_c2 r2,0.45 ke4b w0.3 ke4b w0.4 Lend_c0b r3,0.45 "
        # ENTER on REPLAY: the replay; the cameras, one sample a few seconds; F11 follows the next car (twice), F4 the chase camera, F1 the car's own view, F9 the trackside again
        "k1c w2.5 Lrp_0 r1,0 Prp_0 w5 Lrp_1 r1,0 Prp_1 w5 Lrp_2 r1,0 Prp_2 w5 Lrp_3 r1,0 Prp_3 w5 Lrp_4 r1,0 Prp_4 "
        "k57 w0.8 Lrp_5 r1,0 Prp_5 w4 Lrp_6 r1,0 Prp_6 k57 w0.8 Lrp_7 r1,0 Prp_7 w4 Lrp_8 r1,0 Prp_8 "
        "k3e w0.8 Lrp_9 r1,0 Prp_9 w4 Lrp_10 r1,0 Prp_10 k3b w0.8 Lrp_11 r1,0 Prp_11 w3 k43 w0.8 Lrp_12 r1,0 Prp_12 "
        "w5 Lrp_13 r1,0 Prp_13 w5 Lrp_14 r1,0 Prp_14 w5 Lrp_15 r1,0 Prp_15 w5 Lrp_16 r1,0 Prp_16 w5 Lrp_17 r1,0 Prp_17 w5 Lrp_18 r1,0 Prp_18 w5 Lrp_19 r1,0 Prp_19 "
        "w5 Lrp_20 r1,0 Prp_20 w5 Lrp_21 r1,0 Prp_21 w5 Lrp_22 r1,0 Prp_22 w5 Lrp_23 r1,0 Prp_23 w5 Lrp_24 r1,0 Prp_24 w5 Lrp_25 r1,0 Prp_25 w5 Lrp_26 r1,0 Prp_26 "
        "w5 Lrp_27 r1,0 Prp_27 w5 Lrp_28 r1,0 Prp_28 w5 Lrp_29 r1,0 Prp_29 w5 Lrp_30 r1,0 Prp_30 w5 Lrp_31 r1,0 Prp_31 w5 Lrp_32 r1,0 Prp_32 "
        "q"
    )
    shots = session(steps)
    print(f"{len(shots)} screenshots in {work}")


def read_replay_state(label):
    """replay_<label>.txt of Mem.replay_state(): {"cam": bytes, "obj": bytes, "mode", "flags", "rec": [...], "follow", "cars": [[...] ...]}"""
    st = {"cars": []}
    for line in open(os.path.join(work, "replay_%s.txt" % label)):
        k, _, rest = line.rstrip("\n").partition(" ")
        if k in ("cam", "obj"):
            st[k] = bytes.fromhex(rest)
        elif k == "car":
            st["cars"].append([int(v) for v in rest.split()[1:]])
        elif k == "rec":
            st[k] = [int(v) for v in rest.split()]
        else:
            st[k] = int(rest)
    return st


def wrap_angle(a):
    import math
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def camera_model(st):
    """one sample of the original's camera record against the routines written again (tools/s2/check_replay_s2.c has the firmware's, compared with the same formulas): returns
    (view, shot, kind, consistent, errors) where consistent: the camera looks at the car's object as it is now (a sample taken between the car's step and the camera's does not),
    errors: what differs from the formulas (place and angles) for a consistent sample"""
    import math
    cam, obj = st["cam"], st["obj"]
    view, shot, n = struct.unpack_from("<i", cam, 0xae4)[0], struct.unpack_from("<i", cam, 0x24)[0], struct.unpack_from("<i", cam, 0xa60)[0]
    if view != 1 or not 0 <= shot < n:
        return view, shot, -1, False, {}
    row = struct.unpack_from("<3i3fif", cam, 0x60 + 0x20 * shot)
    kind, fig = row[6], row[7]
    cx, ch, cz = struct.unpack_from("<3i", cam, 0x48)
    pitch, yaw, roll = struct.unpack_from("<3f", cam, 0x54)
    ox, oh, oz = struct.unpack_from("<3i", obj, 0)
    dx, dy, dz = cx - ox, ch - oh, cz - oz
    flat = math.hypot(dx, dz)
    a = abs(math.acos(flat / math.sqrt(dx * dx + dy * dy + dz * dz))) if flat else 0.0
    want_yaw = math.atan2(dz, dx) - math.pi / 2
    want_pitch = a if dy < 0 else -a
    looks = abs(wrap_angle(yaw - want_yaw)) < 0.01 and abs(wrap_angle(pitch - want_pitch)) < 0.01
    err = {}
    if kind == 0:                                       # fixed: the row's place and angles, whatever the car does
        err["place"] = max(abs(cx - row[0]), abs(ch - row[1]), abs(cz - row[2]))
        err["angles"] = max(abs(wrap_angle(pitch - row[3])), abs(wrap_angle(yaw - row[4])), abs(roll - row[5]))
        return view, shot, kind, True, err
    if not looks:
        return view, shot, kind, False, {}
    err["roll"] = abs(roll - (row[5] if kind in (1, 2) else 0.0))
    if kind == 2:                                       # between the car and the row
        share = 0.8 - fig * 0.75
        err["place"] = max(abs(cx - int((row[0] - ox) * share + ox)), abs(ch - int((row[1] - oh) * share + oh)), abs(cz - int((row[2] - oz) * share + oz)))
    elif kind == 3:                                     # 1100 round the car, 440 +- 220 up
        err["place"] = max(abs(flat - 1100.0), 0 if 220 <= dy <= 660 else 1)
    elif kind == 4:                                     # a place from the car turned by the row's heading + pi
        x0, z0, ang = int(fig * 500 + 1320), 0.0, row[4] + math.pi
        tx, tz = z0 * math.sin(ang) + x0 * math.cos(ang), z0 * math.cos(ang) - x0 * math.sin(ang)
        err["place"] = max(abs(cx - (ox + int(tx))), abs(ch - (oh + int(fig * 200 + 550))), abs(cz - (oz + int(tz))))
    return view, shot, kind, True, err


def check_replay(shots):
    """the replay run's screenshots and states against the models: the end screen's choices (REPLAY | RACE AGAIN | EXIT in thirds), the replay's band, and the camera record against the
    formulas of the five kinds of camera; what each shows is printed"""
    sys.path.insert(0, os.path.join(root, "tools"))
    import hud_reference as hr
    import ui_reference as ur
    graf = ur.container(game, "GFX/GRAF1.DAT")
    fnt = ur.fonts(ur.datfile.extract(graf, "MENUL.FNT"))
    local = ur.container(game, "DATA/LOCAL.DAT")
    gtxt = [s for s in ur.datfile.extract(local, "GAME.TXT").decode("latin-1").replace("\r", "").split("\n") if s and not s.startswith("*")]
    strings = [s for s in ur.datfile.extract(local, "STRINGS.TXT").decode("latin-1").replace("\r", "").split("\n") if s and not s.startswith("*")]
    teams = strings[strings.index("CONDOR"):strings.index("CONDOR") + 4]
    hud = hr.Hud(game)
    words = {1: "EXTERNAL", 2: "INCAR", 3: "BACK", 4: "NEAR", 6: "INSIDE"}
    bad = 0
    kinds_ok, kinds_seen, inconsistent = {}, {}, 0
    for png, label in shots:
        name = os.path.basename(png)
        idx = read_png(png)
        if label == "end_cong":                         # the first four seconds: CONGRATULATIONS ! (first place), "1ST POSITION", the total time, from the numbers in the game's memory
            ram = read_ram("end")
            place, ptime = ram[0x119300 + 0xc30], float_of(ram[0x119300 + 0xc38])
            c = hr.Canvas()
            if place == 1:
                c.text_boxed(fnt[1], 1, 2, 0, 320, 70, gtxt[5])
            c.text_boxed(fnt[1], 1, 2, 0, 320, 100, gtxt[place - 1] + " " + gtxt[4])
            c.text_boxed(fnt[1], 1, 2, 0, 320, 130, ur.time_text(ptime))
            drawn = [i for i in range(64000) if c.px[i]]
            d = sum(1 for i in drawn if idx[i] != c.px[i])
            bad += d != 0
            print(f"{name} {label:12s} {'equal' if d == 0 else 'differs by %d of %d' % (d, len(drawn))} ({gtxt[place - 1]} {gtxt[4]}, {ur.time_text(ptime)})")
            continue
        if label.startswith("end_c"):                   # the choices over the band: REPLAY lit or not (the blink), the others dark
            best = None
            for lit in (0, 1, 2, 3):
                c = hr.Canvas()
                for k in range(3):
                    on = lit == k
                    c.text_boxed(fnt[3 if on else 2], 3 if on else 1, 2, 106 * k, 106, 184, gtxt[11 + k])
                drawn = [i for i in range(64000) if c.px[i]]
                d = sum(1 for i in drawn if idx[i] != c.px[i])
                if best is None or d < best[0]:
                    best = (d, lit)
            ok = best[0] == 0
            bad += not ok
            print(f"{name} {label:12s} {'equal' if ok else 'differs by %d' % best[0]} (the choices' text, {'none lit' if best[1] == 3 else 'choice %d lit' % best[1]})")
            continue
        if not label.startswith("rp_"):
            print(f"{name} {label:12s} (looked at, not compared)")
            continue
        st = read_replay_state(label)                   # the same label: the state taken just after the screenshot
        cam = st["cam"]
        view = struct.unpack_from("<i", cam, 0xae4)[0]
        who = st["follow"]
        done = st["cars"][0][0] & 1
        if st["mode"] & 0x100 and not done and who >= 0 and view in words:    # the band: TEAM - VIEW of the car followed while the player's car runs
            text = "%s - %s" % (teams[st["cars"][who][8]], words[view])
            c = hr.Canvas()
            hud.put_text(c, 0, 1, 2, 10, 180, text)
            drawn = [i for i in range(64000) if c.px[i]]
            d = sum(1 for i in drawn if idx[i] != c.px[i])
            ok = d == 0
            bad += not ok
            print(f"{name} {label:12s} band {text!r}: {'equal' if ok else 'differs by %d of %d pixels' % (d, len(drawn))}")
        else:
            print(f"{name} {label:12s} no band expected (mode {st['mode']:#x}, player done {done}, view {view}, follows car {who})")
        v, shot, kind, consistent, err = camera_model(st)
        if kind >= 0:
            kinds_seen[kind] = kinds_seen.get(kind, 0) + 1
            if not consistent:
                inconsistent += 1
            elif all(e < (2.5 if k == "place" else 0.01) for k, e in err.items()):
                kinds_ok[kind] = kinds_ok.get(kind, 0) + 1
            else:
                bad += 1
                print(f"    camera {shot} (kind {kind}) differs from the formulas: {err}")
    for name in ("grid", "fin"):                        # the grid: the player's own chase camera; the finish: the cut to the trackside cameras following the player's car
        try:
            st = read_replay_state(name)
        except FileNotFoundError:
            continue
        view = struct.unpack_from("<i", st["cam"], 0xae4)[0]
        print(f"state {name}: kind of game word {st['mode']:#x}, camera view {view} following car {st['follow']}, player's flags {st['cars'][0][0]:#x}, recorder {st['rec']}")
    print(f"cameras of the replay: kinds seen {dict(sorted(kinds_seen.items()))}, equal to the formulas {dict(sorted(kinds_ok.items()))}, taken between the car's step and the camera's {inconsistent}")
    return bad


def main():
    global work
    if "--replay" in sys.argv:
        work = os.path.join(root, "build", "check", "dosbox_replay")
        if "--check" not in sys.argv:
            replay()
        files = sorted(glob.glob(os.path.join(work, "*.png")))
        labels = open(os.path.join(work, "labels.txt")).read().split()
        bad = check_replay(list(zip(files, labels)))
        print(f"{len(files)} screenshots, {bad} not as the models have them")
        sys.exit(1 if bad else 0)
    if "--league" in sys.argv:
        work = os.path.join(root, "build", "check", "dosbox_league")
        if "--check" not in sys.argv:
            league()
        files = sorted(glob.glob(os.path.join(work, "*.png")))
        labels = open(os.path.join(work, "labels.txt")).read().split()
        bad = check_league(list(zip(files, labels)))
        print(f"{len(files)} screenshots, {bad} not equal to the model")
        sys.exit(1 if bad else 0)
    if "--pages3d" in sys.argv:
        pages3d()
        return
    if "--cars" in sys.argv:
        cars(int(sys.argv[sys.argv.index("--cars") + 1]), "race" in sys.argv, "moves" in sys.argv)
        return
    if "--options" in sys.argv:
        which = next((a for a in sys.argv if a in ("pages", "team2", "team3", "team4")), "")
        if "--check" not in sys.argv:
            options(which)
        work = os.path.join(root, "build", "check", "dosbox_room" + ("_t" + which[4:] if which.startswith("team") else "_" + which if which else ""))
        print("%d screenshots in %s: compared by tools/room.sh" % (len(glob.glob(os.path.join(work, "*.png"))), work))
        return
    if "--timeattack" in sys.argv:
        which = next((a for a in sys.argv if a in ("closed", "race", "free")), "")
        if "--check" not in sys.argv:
            timeattack(which)
        work = os.path.join(root, "build", "check", "dosbox_ta" + ("_" + which if which else ""))
        if which == "free":
            files = sorted(glob.glob(os.path.join(work, "*.png")))
            labels = open(os.path.join(work, "labels.txt")).read().split()
            sys.exit(check_free(list(zip(files, labels))))
        print("%d screenshots in %s: compared by tools/trackmap.sh (the maps, the pages' text, the end screen)" % (len(glob.glob(os.path.join(work, "*.png"))), work))
        return
    extra = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 0
    if "--check" in sys.argv:                   # the screenshots of the last run again (build/check/dosbox)
        files = sorted(glob.glob(os.path.join(work, "*.png")))
        labels = open(os.path.join(work, "labels.txt")).read().split()
        bad = check(list(zip(files, labels)), extra, model())
        print(f"{len(files)} screenshots, {bad} not equal to the model")
        sys.exit(1 if bad else 0)
    atexit.register(drop_junctions)             # (also when the run fails or is stopped: the junctions never outlive the script)
    make_private(extra)
    boot = "w12 k1c w6 k1c w6 k1c w6 k1c w9 Ltitle r3,1.1 k1c w4 Ltrack r3,0.9 "
    moves = "".join("Ke4d R3,0 w2.5 r2,0.9 " for _ in range(extra + 2))
    rest = "k1c w5 Lcar r3,0.9 k1c w1.5 Ltrans r4,0.8 ke4d w1 r4,0.8 q"
    shots = session(boot + moves + rest)
    ref = model()
    bad = check(shots, extra, ref)
    print(f"{len(shots)} screenshots, {bad} not equal to the model")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
