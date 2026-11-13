"""Pick the code for the boot RAM (firmware/link_boot.ld): the input sections the I-cache misses on most per byte, from a trace of the CPU's line reads.

  python -I tools/s2/bootpick.py <program.map> <trace_ib.txt> <frames> <budget bytes> <link_prog.ld> <out link_boot.ld>

The trace is tools/s2/hwvariant.py's (`trace`): one "clock address" line for each line the I-cache read from the SDRAM, of a build with an EMPTY link_boot.ld (code in the
boot RAM does not show in it). <program.map> is that build's. Each address is laid to the input section it lies in (the map's, not a symbol's name: picolibc's sinf is the
symbol _sinf in the section .text.sinf), the sections are taken by misses per byte until the budget is full, and the list names them as the linker script wants them. The
sections of the hot block (link_prog.ld) stay out: they have their own place. firmware/link_check.py checks the list at every build.
docs/screamer2/performance.md, "Where the CPU waits".
"""
import bisect
import collections
import re
import sys


def main():
    mp, tr, frames, budget, prog_ld, out = sys.argv[1], sys.argv[2], float(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
    text = open(mp, encoding="latin1").read()
    secs = []                                   # (address, size, section, file)
    for m in re.finditer(r"^ (\.text[^\s]*)\s*\n?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", text, re.M):
        a, s = int(m.group(2), 16), int(m.group(3), 16)
        if s and a >= 0x10000000:
            secs.append((a, s, m.group(1), m.group(4)))
    secs.sort()
    starts = [a for a, _, _, _ in secs]
    hot = re.search(r"__hot_start = \.;\s*\n([^\n]*)\n\s*__hot_end = \.;", open(prog_ld, encoding="utf-8").read())
    hot = set(re.findall(r"\*\(\.text\.([^\s)]+)\)", hot.group(1))) if hot else set()

    def pattern(sec, fl):
        """how the linker script names the section: by its own name, or (a plain .text of a library's object) by the file"""
        if sec.startswith(".text."):
            return "*(%s)" % sec
        obj = re.search(r"\(([^)]+\.o)\)$", fl)
        return "*%s(.text .text.*)" % obj.group(1) if obj else None

    size = collections.Counter()                # a pattern takes every section of its name: their bytes together
    for a, s, sec, fl in secs:
        p = pattern(sec, fl)
        if p:
            size[p] += s + (-s & 3)
    miss = collections.Counter()
    total = 0
    for line in open(tr):
        a = int(line.split()[1], 16) + 0x10000000
        total += 1
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and a < secs[i][0] + secs[i][1]:
            p = pattern(secs[i][2], secs[i][3])
            if p and secs[i][2][6:] not in hot:
                miss[p] += 1
    order = sorted(miss, key=lambda p: -miss[p] / max(size[p], 32))
    used = got = 0
    picked = []
    for p in order:
        if used + size[p] > budget:
            continue
        picked.append(p)
        used += size[p]
        got += miss[p]
    with open(out, "w", newline="\n") as f:
        f.write("/* The input sections of the code that runs from the boot RAM (link_prog.ld, .bootcode): the %d with the most misses of the I-cache per byte in the replayed race\n"
                "   on track 4 (tools/s2/bootpick.py, a budget of %d bytes: more did not help, the sets of the cache they share matter as much). Made by the tool, checked at every\n"
                "   build by link_check.py; docs/screamer2/performance.md, \"Where the CPU waits\". */\n" % (len(picked), budget))
        for p in picked:
            f.write("        %s\n" % p)
    print("%d sections, %d bytes: %.0f of %.0f misses a frame (%.0f %%)" % (len(picked), used, got / frames, total / frames, 100.0 * got / total))
    for p in picked[:12]:
        print("  %-44s %6.1f a frame, %5d bytes" % (p, miss[p] / frames, size[p]))


main()
