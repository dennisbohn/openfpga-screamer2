"""How many of the hardware's 62 see-through rows each track's files need (firmware/smg.h; the numbers of docs/development.md "Wheel smoke and shadows").

Usage: python -I tools/s2/smg_budget.py <game folder>

For every GFX/MAP<n>H<look>1.DAT (look 0 day, 1 night; the core builds the day) and the four options rooms: the see-through materials of the track's MLB and of MISC.MLB (the
libraries the image builder passes through the step), the distinct texels their textures use in each of the original's five ramps (smoke, shadow, second smoke, brighter, white),
the rows that makes (smg_ref.plan: one row for each, at most 62: the closest pair of a ramp merges), how many texels had to merge, and the bytes the textures' copies add to the image.
Also what a fixed 16 levels for each effect (the quantised table the first plan had) would do to the smoke and the shadow of a track: the share of (texel, colour) pairs whose
palette index differs from the original's row and the mean difference of the colours (six-bit components) when the texels of the smoke's and shadow's ramps are put on 16
equal steps of their ramp.
"""
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
sys.path.insert(0, here)
import datfile
import smg_ref

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]


def records(m):
    end, k = len(m), 0
    while (k + 1) * 16 <= end:
        ro = i32(m, 16 * k + 4)
        if 0 < ro and ro + 12 <= len(m) and ro < end:
            end = ro
        k += 1
    return k


def texels_and_bytes(libs):
    found, copy, seen = set(), 0, set()
    for m in libs:
        for k, routine, ro, w, h, offs in smg_ref.materials(m, records(m), m):
            for o in offs:
                for y in range(h):
                    found.update(m[o + 256 * y:o + 256 * y + w])
            if (id(m), ro) not in seen:
                seen.add((id(m), ro))
                copy += ((8 + 4 * len(offs) + 255) & ~255) + ((max(offs) + 256 * (h - 1) + w - min(offs) + 255) & ~255)
    found.discard(0)
    return found, copy


def track_colours(game, n):
    g = datfile.read(os.path.join(game, "GFX", f"MAP{n}H01.DAT"))
    col = datfile.extract(g, f"MAP{n}.COL")
    d = datfile.read(os.path.join(game, "DATA", f"MAP{n}.DAT"))
    t = datfile.extract(d, f"MAP{n}DAT.DTR").decode("latin-1")
    nums = []
    for line in t[t.index("TRACK_DATA"):].splitlines()[1:]:
        s = line.strip()
        if not s or s.startswith("*"):
            continue
        if s.startswith("END_LIST"):
            break
        nums += [int(v[1:]) for v in s.split() if v[0] == "d"]
        if len(nums) >= 11:
            break
    return [tuple(col[3 * i:3 * i + 3]) for i in range(256)], nums[5:8], nums[8:11]


def quantised(game, n):
    """what 16 equal steps of a ramp, in place of its texels' own levels, would do to the smoke's and the shadow's rows of track n"""
    pal, ast, a2 = track_colours(game, n)
    g = datfile.read(os.path.join(game, "GFX", f"MAP{n}H01.DAT"))
    found, _ = texels_and_bytes([datfile.extract(g, "MISC.MLB")])
    out = []
    for k, rows in ((0, 32), (1, 16), (2, 32)):
        used = [t for t in smg_ref.used_texels(found, smg_ref.SMOKE | smg_ref.SHADOW | smg_ref.SMOKE2) if smg_ref.ramp_of(t) == k]
        first = smg_ref.FIRST_TEXEL[k]
        diff = pairs = ncol = 0
        for t in used:
            l = t - first
            q = first + round(round(l * 15.0 / (rows - 1)) * (rows - 1) / 15.0)
            a, b = smg_ref.original_row(pal, t, ast, a2), smg_ref.original_row(pal, max(q, first + 1) if q == first else q, ast, a2)
            for c in range(256):
                pairs += 1
                diff += a[c] != b[c]
                ncol += sum(abs(x - y) for x, y in zip(pal[a[c]], pal[b[c]])) / 3.0
        if not pairs:
            out.append("%s: none" % ("smoke", "shadow", "second smoke")[k])
            continue
        out.append("%s: %d texels, %.1f %% of the palette indices differ, mean colour difference %.2f of 63" % (("smoke", "shadow", "second smoke")[k], len(used), 100.0 * diff / pairs, ncol / pairs))
    return out


def main():
    game = sys.argv[1]
    print("%-12s %5s  %-26s %5s %6s %8s" % ("container", "found", "smoke/shadow/smoke2/bright/white", "rows", "merged", "copies"))
    names = [f"MAP{n}H{look}1.DAT" for look in (0, 1) for n in range(1, 8) if os.path.exists(os.path.join(game, "GFX", f"MAP{n}H{look}1.DAT"))] + [f"MENU{t}1.DAT" for t in range(1, 5)]
    for nm in names:
        g = datfile.read(os.path.join(game, "GFX", nm))
        es = {e.name.upper() for e in datfile.entries(g)}
        libs = [datfile.extract(g, e) for e in sorted(es) if e.endswith(".MLB")]
        race = not nm.startswith("MENU")
        found, copy = texels_and_bytes(libs)
        ramps = smg_ref.SMOKE | smg_ref.SHADOW | smg_ref.SMOKE2 | smg_ref.BRIGHT | smg_ref.WHITE if race else smg_ref.SHADOW
        used = smg_ref.used_texels(found, ramps)
        by = [sum(1 for t in used if smg_ref.ramp_of(t) == k) for k in range(5)]
        rowmap, alive = smg_ref.plan(found, ramps)
        print("%-12s %5d  %-26s %5d %6d %8d" % (nm, len(found), "/".join(map(str, by)), len(alive), len(used) - len(alive), copy))
    print()
    for n in range(1, 8):
        print("16 equal steps instead of the texels' own levels, track %d: %s" % (n, "; ".join(quantised(game, n))))


main()
