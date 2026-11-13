"""Pick the functions the I-cache misses on most per byte (from the SDRAM trace of the CPU's line reads) for the boot RAM,
and write the linker script's list of their input sections.
Usage: python -I bootpick.py <program.lst> <program.map> <trace_ib.txt> <frames> <budget bytes> <out list.ld>"""
import re, sys, bisect, collections
lst, mp, tr, frames, budget, out = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
starts = []; names = []
for l in open(lst, encoding="latin1"):
    m = re.match(r"^([0-9a-f]{8}) <([^>]+)>:", l)
    if m:
        starts.append(int(m.group(1), 16)); names.append(m.group(2))
o = sorted(range(len(starts)), key=lambda i: starts[i]); starts = [starts[i] for i in o]; names = [names[i] for i in o]
size = {}
for i, n in enumerate(names):
    size[n] = (starts[i + 1] if i + 1 < len(starts) else starts[i] + 4) - starts[i]
text = open(mp, encoding="latin1").read()
own = {}
for m in re.finditer(r"^ (\.text\.[^\s]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", text, re.M):
    own[m.group(1)] = int(m.group(3), 16)
cnt = collections.Counter()
for l in open(tr):
    c, a = l.split()
    a = int(a, 16) + 0x10000000
    i = bisect.bisect_right(starts, a) - 1
    cnt[names[i] if i >= 0 else "?"] += 1
tot = sum(cnt.values())
rows = []
for n, m in cnt.items():
    sec = ".text." + n
    if sec in own:
        rows.append((n, m, own[sec]))
rows.sort(key=lambda r: -r[1] / max(r[2], 32))
used = 0; picked = []; saved = 0
for n, m, s in rows:
    if used + s + 32 > budget: continue
    picked.append((n, m, s)); used += s + 32; saved += m
# the libgcc save/restore helpers have no sections of their own: one object
lines = ["        *save-restore.o(.text .text.*)"]
for n, m, s in picked:
    if n.startswith("__riscv_"): continue
    lines.append("        *(.text.%s)" % n)
open(out, "w").write("\n".join(lines) + "\n")
print("functions", len(picked), "bytes ~", used, "misses/frame %.0f of %.0f (%.0f %%)" % (saved / frames, tot / frames, 100.0 * saved / tot))
