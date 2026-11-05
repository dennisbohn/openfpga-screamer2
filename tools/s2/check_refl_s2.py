"""The reflection's picture by the kind of the ground, worked out from STARTL.EXE's own code (the table of s2_env_phase() in firmware/s2env.c).

The game's routine 0x1f1ee ends in a chain of compares on the kind under the car's fourth wheel (STARTL.EXE 0x1f372 .. 0x1f3c4); each case stores the phase of the
reflection's picture into the car's +0xc80 and, for the player's car, a rate into the constant at 0xb0678. This reads that code from the flat image (build/exe/
startl_fix.bin: tools/dis.sh -f makes it) with a decoder for exactly the few instructions the chain uses, follows it for every kind 0..255 and prints one line a
kind: `<kind> <phase hex> <rate>`. tools/s2/check_refl_s2.sh compares that with what the firmware's function returns.

Usage: python -I tools/s2/check_refl_s2.py [flat image]
"""
import os
import struct
import sys

CHAIN = 0x1F372            # the chain of compares on the kind (the record's +0x6b0, kept at [ebp-8])


def rel8(d, a):
    return a + 2 + struct.unpack_from("<b", d, a + 1)[0]


def rel32(d, a, n):
    return a + n + struct.unpack_from("<i", d, a + n - 4)[0]


def target(d, kind):
    """the case a kind reaches: the compares and jumps of the chain from CHAIN, with the flags a `cmp [ebp-8], imm8` leaves (below, equal)"""
    a, below, equal = CHAIN, False, False
    for _ in range(64):
        op = d[a]
        if d[a:a + 3] == b"\x83\x7d\xf8":              # cmp dword [ebp-8], imm8
            imm = d[a + 3]
            below, equal = kind < imm, kind == imm
            a += 4
        elif op == 0x72:                                 # jb rel8
            a = rel8(d, a) if below else a + 2
        elif op == 0x74:                                 # je rel8
            a = rel8(d, a) if equal else a + 2
        elif op == 0x76:                                 # jbe rel8
            a = rel8(d, a) if below or equal else a + 2
        elif op == 0xEB:                                 # jmp rel8
            a = rel8(d, a)
        elif d[a:a + 2] == b"\x0f\x86":                  # jbe rel32
            a = rel32(d, a, 6) if below or equal else a + 6
        elif d[a:a + 2] == b"\x0f\x84":                  # je rel32
            a = rel32(d, a, 6) if equal else a + 6
        else:
            return a                                     # not a compare or a jump: the case's own code
    raise SystemExit("the chain does not end for kind %d" % kind)


def case(d, a):
    """the phase and rate a case stores: `mov dword [eax+0xc80], imm32` and `mov dword [0xb0678], imm32`, up to the case's jump to the exit (the player's
    cars' rate is stored after a compare of the car with the player's: the bytes are scanned, not run)"""
    phase = rate = None
    for _ in range(40):
        if d[a:a + 6] == b"\xc7\x80\x80\x0c\x00\x00" and phase is None:
            phase = struct.unpack_from("<I", d, a + 6)[0]
            a += 10
        elif d[a:a + 6] == b"\xc7\x05\x78\x06\x0b\x00" and rate is None:
            rate = struct.unpack_from("<f", d, a + 6)[0]
            a += 10
        elif d[a] in (0xE9, 0xEB):                       # the case's jump to the exit
            break
        else:
            a += 1
    return phase, rate


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "exe", "startl_fix.bin")
    d = open(path, "rb").read()
    for kind in range(256):
        phase, rate = case(d, target(d, kind))
        assert phase is not None, "kind %d: no phase stored" % kind
        print("%d %x %.4f" % (kind, phase, rate if rate is not None else 0.0))


main()
