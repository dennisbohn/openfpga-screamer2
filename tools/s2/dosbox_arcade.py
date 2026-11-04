"""The original's arcade pages in DOSBox against the firmware's model of them (docs/screamer2/menus.md "The arcade pages").

Usage: python -I tools/s2/dosbox_arcade.py [extra tracks 0..4] [--check]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2"; --check: the
screenshots of the last run again, without DOSBox)

Starts STARTL.EXE in the game folder's own DOSBox on a private copy of the game folder (build/dosgame: the data folders are junctions, removed again
when the script ends; CONFIG is a copy whose CHOICE.DAT has the number of extra tracks open: the owner's own is never touched), types its way with scan codes through TEAM SELECTION, the
name and the OPTIONS ROOM to ARCADE, and takes Ctrl+F5 screenshots (320 x 200 palette indices, the raw frame) on the title, the TRACK page, the CAR page and
the TRANSMISSION page. Each is then compared with the frames of tools/ui_arcade_ref.py's model (the page drawn by an independent reading of the
program): the TRACK page whole, the others in the parts the model draws (the original also has the 3D cars: the title's band with the prompt, the CAR
page's title, badge and the left half of the plate, the TRANSMISSION page's title, badge and buttons). DOSBox's window must stay in front while it runs
(about 2 minutes; the keys go to the window in front).
Output: build/check/dosbox/*.png (never committed) and one line per screenshot; exit code 0 if every one equals a frame of the model.
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


def key(sc, up, ext=False):
    i = INPUT(1, KI(0, sc, 8 | (2 if up else 0) | (1 if ext else 0), 0, 0))
    user32.SendInput(1, ctypes.byref(i), ctypes.sizeof(i))


def press(sc, ext=False):
    key(sc, 0, ext)
    time.sleep(0.12)
    key(sc, 1, ext)
    time.sleep(0.12)


def dosbox_pid():
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

def choice_with_extra_tracks(src, dst, extra):
    """CONFIG/CHOICE.DAT: word i is kept as (v + i * 0x1050c) ^ i; word 10 (+0x28) is the tracks opened beyond the first three"""
    d = open(src, "rb").read()
    w = list(struct.unpack("<%dI" % (len(d) // 4), d))
    dec = [((v ^ i) - i * 0x1050C) & 0xFFFFFFFF for i, v in enumerate(w)]
    dec[10] = extra
    dec[0xB0 // 4 + 1] = 1                      # the first team's fifth car won
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


def make_private(extra):
    drop_junctions()
    shutil.rmtree(private, ignore_errors=True)
    os.makedirs(private)
    for name in os.listdir(game):
        s, d = os.path.join(game, name), os.path.join(private, name)
        if name.upper() in ("DOSBOX", "GAME.GOG", "GAME.DAT", "SCREAMER2.ZIP") or name.lower().endswith((".lnk", ".dll")):
            continue
        if name.upper() == "CONFIG":
            shutil.copytree(s, d)
            choice_with_extra_tracks(os.path.join(s, "CHOICE.DAT"), os.path.join(d, "CHOICE.DAT"), extra)
        elif os.path.isdir(s):
            subprocess.run(["cmd", "/c", "mklink", "/J", d, s], check=True, capture_output=True)
        else:
            shutil.copy2(s, d)


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
    time.sleep(2)
    pid = dosbox_pid() or p.pid
    label, shots = "", []
    for st in steps.split():
        if st[0] == "w":
            time.sleep(float(st[1:]))
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
    assert len(files) == len(shots), f"{len(files)} screenshots for {len(shots)} requests"
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
        m = (ref.title(s), ref.track(s, nopen), ref.car(s, 1), ref.trans(s))[page]
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


def main():
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
