"""Which code moved between two builds of the program (program.map of tools/fw.sh), for the instruction cache's layout.

Usage: python -I tools/s2/map_shift.py <old program.map> <new program.map>

The I-cache is 8 KB and direct mapped (docs/screamer2/performance.md section 7: the hot code of the piece loop sits together at the start of
the program because two hot pieces a multiple of 8 KB apart throw each other out on every piece; padding of 64 to 2,000 bytes between main.c's
code and the rest cost 0.6 ms a frame in that tree). Whoever adds or grows code in front of the frame's code shifts the objects behind it. This
lists, per object file, how far its functions moved (new address - old address), so that a change can be judged: a shift of 0
everywhere, or one the race's frame time (the simulation: tools/s2/sim_race.sh) shows to be harmless.
"""
import re
import sys


def functions(path):
    """{section name: (address, size, object)} of the .text.* input sections of the map"""
    lines = open(path).read().split("\n")
    out, i = {}, 0
    while i < len(lines):
        m = re.match(r"^ (\.text\.\S+)\s*$", lines[i])                       # a long name: its numbers are on the next line
        if m and i + 1 < len(lines):
            m2 = re.match(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", lines[i + 1])
            if m2:
                out[m.group(1)] = (int(m2.group(1), 16), int(m2.group(2), 16), m2.group(3).split("/")[-1])
            i += 2
            continue
        m = re.match(r"^ (\.text\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", lines[i])
        if m:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), m.group(4).split("/")[-1])
        i += 1
    return out


def main():
    old, new = functions(sys.argv[1]), functions(sys.argv[2])
    moved, same, added, gone = {}, 0, [], []
    for name, (addr, size, obj) in new.items():
        if name not in old:
            added.append((obj, name, size))
        elif old[name][0] != addr:
            moved.setdefault(obj, set()).add(addr - old[name][0])
        else:
            same += 1
    gone = [(o[2], n) for n, o in old.items() if n not in new]
    print(f"{same} functions at the same address, {sum(len(v) > 0 for v in moved.values())} objects moved, {len(added)} functions new, {len(gone)} gone")
    for obj, shifts in sorted(moved.items()):
        print(f"  moved  {obj}: {', '.join(f'{s:+d}' for s in sorted(shifts)[:6])}{' ...' if len(shifts) > 6 else ''}")
    for obj, name, size in sorted(added):
        print(f"  new    {obj} {name} ({size} bytes)")
    for obj, name in sorted(gone):
        print(f"  gone   {obj} {name}")


main()
