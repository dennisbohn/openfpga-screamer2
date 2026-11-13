"""I-cache misses per function from the trace of tools/s2/hwvariant.py (trace): python -I tools/s2/imisses.py <build/firmware-x/program.lst> <build/sim/trace_ib.txt> [frames]
The trace has the SDRAM line reads only: code in the boot RAM does not show."""
import re, sys, bisect, collections
lst, tr = sys.argv[1], sys.argv[2]
frames = float(sys.argv[3]) if len(sys.argv) > 3 else 76
starts = []
names = []
for l in open(lst, encoding="latin1"):
    m = re.match(r"^([0-9a-f]{8}) <([^>]+)>:", l)
    if m:
        starts.append(int(m.group(1), 16))
        names.append(m.group(2))
order = sorted(range(len(starts)), key=lambda i: starts[i])
starts = [starts[i] for i in order]
names = [names[i] for i in order]
cnt = collections.Counter()
sets = collections.Counter()
n = 0
t0 = t1 = None
for l in open(tr):
    c, a = l.split()
    a = int(a, 16) + 0x10000000
    i = bisect.bisect_right(starts, a) - 1
    cnt[names[i] if i >= 0 else "?"] += 1
    n += 1
    t0 = t0 or int(c)
    t1 = int(c)
print("misses", n, "per frame %.0f" % (n / frames), "cycles %.1f M" % ((t1 - t0) / 1e6))
tot = 0
for k, v in cnt.most_common(45):
    tot += v
    print("%-34s %8d  %6.1f /frame  %5.1f %%" % (k, v, v / frames, 100.0 * v / n))
