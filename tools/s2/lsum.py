"""Summary of the L lines of the replay simulations' debug.txt (tools/s2/sim_race.sh build/sim/<name>_log.txt): python -I tools/s2/lsum.py <log> ..."""
import re, sys, statistics as st
for f in sys.argv[1:]:
    d = open(f, "rb").read().replace(b"\0", b"").decode("latin1")
    rows = []
    for l in d.split("\n"):
        m = re.match(r"L(\d+\.\d) ([\d.]+) k(\d+) a([\d.]+) c([\d.]+) s([\d.]+) o([\d.]+)(?: i([\d.]+))? w([\d.]+) p(\d+) n(\d+) m(\d+)", l)
        if m:
            g = list(m.groups())
            if g[7] is None: g[7] = "nan"
            rows.append([float(x) for x in g])
    head = [l for l in d.split("\n") if l.startswith("T4 drv")]
    h = head[0][:70] if head else "-"
    if not rows:
        print("%-24s no L lines | %s" % (f.split("/")[-1], h))
        continue
    t = [r[1] for r in rows]
    n = len(rows)
    iv = [r[7] for r in rows if r[7] == r[7]]
    print("%-24s n%2d mean %.2f max %.1f >20:%2d >19:%2d | a%.2f c%.2f s%.2f o%.2f%s w%.2f | %s" % (
        f.split("/")[-1], n, st.mean(t), max(t), sum(x > 20 for x in t), sum(x > 19 for x in t),
        st.mean(r[3] for r in rows), st.mean(r[4] for r in rows), st.mean(r[5] for r in rows), st.mean(r[6] for r in rows),
        (" (in scene %.2f)" % st.mean(iv)) if iv else "", st.mean(r[8] for r in rows), h))
