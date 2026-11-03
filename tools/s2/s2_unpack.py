"""Screamer 2 experiment (docs/screamer2/comparison.md): do our container reader and LZW
(tools/datfile.py, unchanged) unpack every Screamer 2 .DAT?

Usage: python tools/s2/s2_unpack.py <Screamer 2 folder> [out dir] [-j jobs] [pattern ...]
Reads every *.DAT below GFX, DATA, ANIMS (and CONFIG/CHOICE.DAT, SETUP.DAT), unpacks each
entry with datfile.extract's own code, checks the size against the directory and prints one
line per container: entries, bytes unpacked, methods used, failures. With an out dir the
entries are written to out/<container>/<name> (game data: keep it under build/).
"""
import glob
import os
import sys
import time
from collections import Counter
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile


def do(args):
    path, out = args
    t0 = time.time()
    data = datfile.read(path)
    try:
        ents = datfile.entries(data)
    except Exception as e:                      # not a container (CHOICE.DAT is a save file)
        return path, None, str(e), 0, 0, 0
    bad, total, methods = [], 0, Counter()
    for e in ents:
        methods[e.method] += 1
        blob = data[e.offset:e.offset + e.stored]
        try:
            res = blob[:e.size] if e.method == 0 else datfile.lzw(blob, e.size)
            if len(res) != e.size:
                bad.append(f"{e.name}: {len(res)} of {e.size}")
            total += len(res)
            if out:
                d = os.path.join(out, os.path.basename(path)[:-4])
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, e.name), "wb") as fh:
                    fh.write(res)
        except Exception as ex:
            bad.append(f"{e.name}: {ex}")
    return path, len(ents), dict(methods), total, bad, time.time() - t0


def main():
    args = sys.argv[1:]
    jobs = 4
    if "-j" in args:
        i = args.index("-j")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    root = args[0]
    out = args[1] if len(args) > 1 and not args[1].startswith("+") else None
    pats = [a[1:] for a in args[1:] if a.startswith("+")]
    files = sorted(glob.glob(os.path.join(root, "GFX", "*.DAT")) + glob.glob(os.path.join(root, "DATA", "*.DAT"))
                   + glob.glob(os.path.join(root, "ANIMS", "*.DAT")) + glob.glob(os.path.join(root, "CONFIG", "*.DAT")))
    if pats:
        files = [f for f in files if any(p in f.replace("\\", "/") for p in pats)]
    with Pool(jobs) as p:
        n = tot = fails = 0
        for path, cnt, methods, total, bad, secs in p.imap(do, [(f, out) for f in files]):
            name = os.path.relpath(path, root).replace("\\", "/")
            if cnt is None:
                print(f"{name:22s} not a container ({methods})")
                continue
            n += cnt
            tot += total
            fails += len(bad)
            print(f"{name:22s} {cnt:4d} entries {total:10d} bytes  methods {methods}  {secs:5.1f}s  "
                  f"{'OK' if not bad else 'FAIL ' + '; '.join(bad[:3])}")
    print(f"{len(files)} files, {n} entries, {tot} bytes unpacked, {fails} failures")


if __name__ == "__main__":
    main()
