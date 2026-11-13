"""Hardware variants of the SoC for the simulation only (nothing in core/ is changed): the CPU's cache sizes and ways, the SDRAM arbiter's order, counters and a trace of the CPU's
line reads in the SDRAM controller. Each variant gets a testbench directory sim/soc_<name>/ (git-ignored) that tools/s2/sim_race_tb.sh runs like tools/s2/sim_race.sh runs `soc`.
(docs/screamer2/performance.md, "Where the CPU waits")

Usage: python -I tools/s2/hwvariant.py <name>=<spec> ...
  spec: comma list of  i<KB>[w<ways>]  (the I-cache, e.g. i16w2: 16 KB, 2 ways; i8w2: two ways of 4 KB, no more block RAM)
                       d<KB>[w<ways>]  (the D-cache)
                       p1              (the arbiter takes the iBus before the geometry unit's and rasterizer's lines; core/src/fpga/core/sdram_port.v: dBus > rasterizer > iBus)
                       pdyn, ptgt      (the CPU's branch prediction DYNAMIC or DYNAMIC_TARGET instead of STATIC)
                       early           (branches resolved in the execute stage: BranchPlugin earlyBranch = true)
                       stat            (a line every 2^20 clocks: the controller's busy clocks, the jobs of each port, the clocks an I-cache miss waits)
                       trace[=<from>]  (the addresses of the CPU's line reads from clock <from> on into build/sim/trace_ib.txt and trace_db.txt)
  e.g.   python -I tools/s2/hwvariant.py i8w2p1=i8w2,p1 i16w2=i16w2
The CPU variants come from tools/vexriscv/gen.sh (PocketVex.scala changed for the run and put back; build/vexriscv has to be there: the script's first run downloads it).
"""
import os, re, shutil, subprocess, sys

root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
scala = os.path.join(root, "tools", "vexriscv", "PocketVex.scala")
core_cpu = os.path.join(root, "core", "src", "fpga", "core", "cpu", "VexPocketIMFC.v")
I_OLD = "        cacheSize          = 8192,\n        bytePerLine        = 32,\n        wayCount           = 1,"
D_OLD = "        cacheSize        = 8192,\n        bytePerLine      = 32,\n        wayCount         = 1,"
P_OLD = "      prediction           = STATIC,"
E_OLD = "new BranchPlugin(earlyBranch = false,"
STAT = """
// ---- simulation only (tools/s2/hwvariant.py): where the controller's time goes, a line every 2^20 clocks
reg [31:0] x_cyc = 0, x_busy = 0, x_br = 0, x_dbr = 0, x_dbw = 0, x_rs = 0, x_ib = 0, x_ibout = 0, x_dbout = 0, x_rsw = 0, x_ibw = 0, x_dbwt = 0;
always @(posedge clk) begin
    x_cyc <= x_cyc + 1;
    if(!job_ready) x_busy <= x_busy + 1;
    if(job_take && sel_br) x_br <= x_br + 1;
    if(job_take && sel_db && !db_wr) x_dbr <= x_dbr + 1;
    if(job_take && sel_db && db_wr) x_dbw <= x_dbw + 1;
    if(job_take && sel_rs) x_rs <= x_rs + 1;
    if(job_take && sel_ib) x_ib <= x_ib + 1;
    if(ib_valid || ib_busy) x_ibout <= x_ibout + 1;      // an I-cache miss outstanding: the CPU waits for it
    if(db_busy || (db_valid && !db_wr)) x_dbout <= x_dbout + 1;
    if(ib_valid) x_ibw <= x_ibw + 1;                     // waiting for the arbiter
    if(db_valid) x_dbwt <= x_dbwt + 1;
    if(rs_valid) x_rsw <= x_rsw + 1;
    if(x_cyc[19:0] == 20'd0 && x_cyc != 0)
        $display("SDRAMSTAT %0d busy %0d br %0d dbr %0d dbw %0d rs %0d ib %0d | ibout %0d dbout %0d | wait ib %0d db %0d rs %0d", x_cyc >> 20, x_busy, x_br, x_dbr, x_dbw, x_rs, x_ib, x_ibout, x_dbout, x_ibw, x_dbwt, x_rsw);
end
"""
TRACE = """
// ---- simulation only (tools/s2/hwvariant.py): the addresses of the CPU's line reads from clock %d on
integer x_fi, x_fd;
initial begin x_fi = $fopen("build/sim/trace_ib.txt", "w"); x_fd = $fopen("build/sim/trace_db.txt", "w"); end
always @(posedge clk) begin
    if(x_cyc >= 32'd%d && job_take && sel_ib) $fwrite(x_fi, "%%0d %%x%sn", x_cyc, {ib_addr, 5'b00000});
    if(x_cyc >= 32'd%d && job_take && sel_db && !db_wr) $fwrite(x_fd, "%%0d %%x%sn", x_cyc, {db_addr, 2'b00});
end
"""


def cpu_variant(tag, isz, iw, dsz, dw, pred, early):
    out = os.path.join(root, "build", "vexvar", tag, "cpu")
    if os.path.exists(os.path.join(out, "VexPocketIMFC.v")):
        return out
    orig = open(scala, encoding="utf-8").read()
    assert I_OLD in orig and D_OLD in orig and P_OLD in orig and E_OLD in orig
    s = orig.replace(I_OLD, "        cacheSize          = %d,\n        bytePerLine        = 32,\n        wayCount           = %d," % (isz * 1024, iw))
    s = s.replace(D_OLD, "        cacheSize        = %d,\n        bytePerLine      = 32,\n        wayCount         = %d," % (dsz * 1024, dw))
    s = s.replace(P_OLD, "      prediction           = %s," % pred)
    if early:
        s = s.replace(E_OLD, "new BranchPlugin(earlyBranch = true,")
    try:
        open(scala, "w", encoding="utf-8", newline="\n").write(s)
        r = subprocess.run(["sh", os.path.join(root, "tools", "vexriscv", "gen.sh")], cwd=root, capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stdout[-800:] + r.stderr[-800:])
        os.makedirs(out, exist_ok=True)
        shutil.copy(os.path.join(root, "build", "vexriscv", "out", "imfc", "VexPocketIMFC.v"), out)
    finally:
        open(scala, "w", encoding="utf-8", newline="\n").write(orig)
        subprocess.run(["git", "checkout", "--", "core/src/fpga/core/cpu/VexPocketIMFC.v", "tools/vexriscv/PocketVex.scala"], cwd=root)
    return out


def main():
    for arg in sys.argv[1:]:
        name, spec = arg.split("=", 1)
        isz, iw, dsz, dw, p1, stat, trace, pred, early = 8, 1, 8, 1, False, False, None, "STATIC", False
        for part in spec.split(","):
            m = re.fullmatch(r"([id])(\d+)(?:w(\d))?", part)
            if m:
                if m[1] == "i": isz, iw = int(m[2]), int(m[3] or 1)
                else: dsz, dw = int(m[2]), int(m[3] or 1)
            elif part == "p1": p1 = True
            elif part == "pdyn": pred = "DYNAMIC"
            elif part == "ptgt": pred = "DYNAMIC_TARGET"
            elif part == "early": early = True
            elif part == "stat": stat = True
            elif part.startswith("trace"):
                trace = int(part.split("=")[1]) if "=" in part else 0
            else:
                sys.exit("unknown part " + part)
        files = open(os.path.join(root, "sim", "soc", "files.f")).read()
        d = os.path.join(root, "sim", "soc_" + name)
        os.makedirs(d, exist_ok=True)
        lint = ["sim/lint.vlt"]
        if (isz, iw, dsz, dw, pred, early) != (8, 1, 8, 1, "STATIC", False):
            cpu = cpu_variant("i%dw%d_d%dw%d%s%s" % (isz, iw, dsz, dw, "" if pred == "STATIC" else "_" + pred.lower(), "_early" if early else ""), isz, iw, dsz, dw, pred, early)
            files = files.replace("core/src/fpga/core/cpu/VexPocketIMFC.v", os.path.relpath(os.path.join(cpu, "VexPocketIMFC.v"), root).replace("\\", "/"))
            open(os.path.join(d, "lint.vlt"), "w", newline="\n").write('`verilator_config\nlint_off -file "build/vexvar/*"\n')
            files = files.replace("sim/lint.vlt", "sim/lint.vlt\nsim/soc_%s/lint.vlt" % name, 1)
        if p1 or stat or trace is not None:
            s = open(os.path.join(root, "core", "src", "fpga", "core", "sdram_port.v")).read()
            if p1:
                a = ("    wire        sel_rs = !sel_br && !brd_valid && !db_valid && rs_valid;\n"
                     "    wire        sel_ib = !sel_br && !brd_valid && !db_valid && !rs_valid && ib_valid;")
                b = ("    wire        sel_ib = !sel_br && !brd_valid && !db_valid && ib_valid;\n"
                     "    wire        sel_rs = !sel_br && !brd_valid && !db_valid && !ib_valid && rs_valid;")
                assert a in s
                s = s.replace(a, b)
            add = (STAT if (stat or trace is not None) else "") + (TRACE % (trace, trace, chr(92), trace, chr(92)) if trace is not None else "")
            i = s.rindex("endmodule")
            s = s[:i] + add + s[i:]
            sd = os.path.join(root, "build", "vexvar", "sdram_" + name)
            os.makedirs(sd, exist_ok=True)
            open(os.path.join(sd, "sdram_port.v"), "w", newline="\n").write(s)
            files = files.replace("core/src/fpga/core/sdram_port.v", "build/vexvar/sdram_%s/sdram_port.v" % name)
        open(os.path.join(d, "files.f"), "w", newline="\n").write(files)
        shutil.copy(os.path.join(root, "sim", "soc", "tb.cpp"), os.path.join(d, "tb.cpp"))
        print("sim/soc_%s: I-cache %d KB x %d ways, D-cache %d KB x %d%s%s%s" % (name, isz, iw, dsz, dw, ", arbiter iBus first" if p1 else "", ", counters" if stat or trace is not None else "",
                                                                         ", trace" if trace is not None else ""))


main()
