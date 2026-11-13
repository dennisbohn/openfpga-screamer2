"""Check the two lists of link_prog.ld against the link's result (the Makefile runs this before program.bin is made). A pattern in a linker script that matches no section is no
error to the linker, and a function that is renamed, made inline or spelled wrong would silently leave its list.

  python3 link_check.py <program.map> <program.lst> <link_boot.ld> <link_prog.ld>

1. Every pattern of link_boot.ld has put a section into the boot RAM (.bootcode).
2. Every section the hot block names (the line after __hot_start in link_prog.ld) lies in the hot block, and none of them is also in link_boot.ld (.bootcode comes first and
   would take it).
3. No function of the hot block calls a function outside it, but for the few known ones (ALLOWED: none of them runs for an ordinary piece). A call into other code for every
   piece throws lines of the piece loop out of the I-cache (docs/screamer2/performance.md, "Where the CPU waits").
"""
import re
import sys

ALLOWED = {
    "gu_draw": "the PC's model of the geometry unit, never on the hardware",
    "gu_split": "a block over the unit's limits, cut into runs",
    "multi_pieces": "a mesh with several blocks or moving parts",
    "track_view": "once a pass",
    "track_view_mirror": "once a pass",
    "dlog_str": "hw_piece's message when a block could not be cut",
}


def main():
    mp, lst, boot_ld, prog_ld = sys.argv[1:5]
    text = open(mp, encoding="latin1").read()
    sym = {m.group(2): int(m.group(1), 16) for m in re.finditer(r"^\s+0x([0-9a-f]+)\s+(__[a-z_]+) = ", text, re.M)}
    need = ("__hot_start", "__hot_end", "__boot_start", "__boot_end")
    if any(k not in sym for k in need):
        sys.exit("link_check: the map has no " + ", ".join(k for k in need if k not in sym))
    hot0, hot1, boot0, boot1 = (sym[k] for k in need)
    # the input sections placed: " .text.name  0xaddr  0xsize file" (the name alone on its line when it is long)
    placed = {}
    for m in re.finditer(r"^ (\.text[^\s]*)\s*\n?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", text, re.M):
        addr, size = int(m.group(2), 16), int(m.group(3), 16)
        if size:
            placed.setdefault(m.group(1), []).append((addr, size, m.group(4)))
    errors = []

    boot_names = []
    for line in open(boot_ld, encoding="utf-8"):
        line = line.split("/*")[0].strip()
        if not line or line.startswith("*/") or not line.startswith("*"):
            continue
        m = re.fullmatch(r"\*\(\.text\.([^\s)]+)\)", line)
        f = re.fullmatch(r"\*([^\s(]+)\(\.text \.text\.\*\)", line)
        if m:
            boot_names.append(m.group(1))
            if not any(boot0 <= a < boot1 for a, _, _ in placed.get(".text." + m.group(1), [])):
                errors.append("link_boot.ld names .text.%s, and no such section went into the boot RAM (renamed, inline now, or the name is the symbol's and not the section's?)" % m.group(1))
        elif f:
            if not any(boot0 <= a < boot1 and f.group(1) in fl for secs in placed.values() for a, _, fl in secs):
                errors.append("link_boot.ld names the file %s, and nothing of it went into the boot RAM" % f.group(1))
        else:
            errors.append("link_boot.ld: a line this check does not know: " + line)

    prog = open(prog_ld, encoding="utf-8").read()
    m = re.search(r"__hot_start = \.;\s*\n([^\n]*)\n\s*__hot_end = \.;", prog)
    if not m:
        sys.exit("link_check: link_prog.ld has no line of sections between __hot_start and __hot_end")
    hot_names = re.findall(r"\*\(\.text\.([^\s)]+)\)", m.group(1))
    for n in hot_names:
        if n in boot_names:
            errors.append("%s is in the hot block and in link_boot.ld: the boot RAM takes it" % n)
        elif not any(hot0 <= a < hot1 for a, _, _ in placed.get(".text." + n, [])):
            errors.append("the hot block names .text.%s, and no such section lies in it (renamed or inline now?)" % n)

    cur, cur_addr = None, 0
    for line in open(lst, encoding="latin1"):
        f = re.match(r"^([0-9a-f]{8}) <([^>]+)>:", line)
        if f:
            cur_addr, cur = int(f.group(1), 16), f.group(2)
            continue
        if cur is None or not hot0 <= cur_addr < hot1:
            continue
        c = re.search(r"\bjal\b\s+(?:\w+,)?\s*([0-9a-f]+) <([^>+]+)>", line)
        if c:
            target, name = int(c.group(1), 16), c.group(2)
            if not hot0 <= target < hot1 and name not in ALLOWED and not name.startswith(("prof_", "pf_")):
                errors.append("%s (hot block) calls %s, which lies outside it: put it into the block, make it inline, or name it in link_check.py's ALLOWED with the reason" % (cur, name))

    print("link_check: hot block %d of 8192 bytes (%s); boot RAM code %d of 24576 bytes (%d patterns)" % (hot1 - hot0, ", ".join(hot_names), boot1 - boot0, len(boot_names) + 1))
    if errors:
        for e in dict.fromkeys(errors):
            print("link_check: " + e)
        sys.exit(1)


main()
