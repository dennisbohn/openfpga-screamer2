"""Makes a copy of firmware/hw.h usable on the PC with the model of tools/host/music_hw.c: the data slot's and the mixer's music registers go
through model_reg() (which sees what the CPU writes and answers with the simulated hardware's state), rdcycle() reads the model's clock, and the RISC-V
instructions of the rest of the file are dropped (as tools/menu_screens.sh does). Usage: python -I music_hw_patch.py <hw.h of the copy>"""
import sys

REGS = [("DS_ID", "0xF0000020u"), ("DS_OFFSET", "0xF0000024u"), ("DS_ADDR", "0xF0000028u"), ("DS_LENGTH", "0xF000002Cu"),
        ("DS_CTRL", "0xF0000030u"), ("DS_SIZE", "0xF0000034u"), ("AU_MUSIC_ADDR", "0xF000440Cu"), ("AU_MUSIC_VOL", "0xF0004410u"),
        ("AU_MUSIC", "0xF0004414u")]

p = sys.argv[1]
t = open(p).read()
t = t[:t.index("/* Cache maintenance")] + """static inline void dcache_invalidate(void) {}
static inline void icache_sync(void) {}
static inline uint32_t rdcycle(void) { return mhw_cycle(); }
"""
for name, addr in REGS:
    old = "#define %-15s REG32(%s)" % (name, addr)
    if t.count(old) != 1:
        sys.exit("hw.h has changed: %s is not defined as expected" % name)
    t = t.replace(old, "#define %-15s (*model_reg(%s))" % (name, addr))
t = t.replace("#define REG32(addr)", "volatile uint32_t *model_reg(uint32_t addr);\nuint32_t mhw_cycle(void);\n#define REG32(addr)", 1)
open(p, "w").write(t)
