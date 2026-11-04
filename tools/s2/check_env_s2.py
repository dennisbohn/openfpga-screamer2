"""Check the windows' reflection picture of a Screamer 2 track image (tools/s2/make_track_s2.py, firmware/track_build.c).

For every routine-160 material of the cars' records (the section in front of the MLB) the image's texture record
must be the one of the reflection (ENV.MLB of GFX/ENVP{n}{look}1.DAT), and for every frame the hardware's texel
(u and v masked with 0xff, rows of 256 bytes) must be the original's (STARTL.EXE 0xcd771: the texel's address
masked with 0x7f7f, the picture in the first 128 columns of 128 rows of 256 bytes) for every u, v the reflection
can produce (whole texels, -512 to 511). The frame of a phase is n x phase >> 16 in both, with the same n.

Usage: python -I tools/s2/check_env_s2.py <Screamer 2 folder> <image.bin> <track 1..7> [look 0|1]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]


def main():
    root, image, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
    look = sys.argv[4] if len(sys.argv) > 4 else "0"
    img = open(image, "rb").read()
    mlb = i32(img, 0x08)                    # the MLB section; the car materials lie in front, from 0x100
    env = datfile.extract(datfile.read(os.path.join(root, "GFX", f"ENVP{n}{look}1.DAT")), "ENV.MLB")
    ro = i32(env, 4)
    frames = i32(env, ro) & 0xFFFF
    src = [i32(env, ro + 8 + 4 * k) for k in range(frames)]
    windows = [k for k in range((mlb - 0x100) // 16) if i32(img, 0x100 + 16 * k) == 160]
    assert windows, "no routine-160 material in the car materials"
    for k in windows:
        rec = mlb + i32(img, 0x100 + 16 * k + 4)
        assert i32(img, rec) == frames, f"material {k}: {i32(img, rec)} frames, ENV.MLB has {frames}"
        for fr in range(frames):
            pic = mlb + i32(img, rec + 8 + 4 * fr)
            bad = 0
            for v in range(-512, 512):
                for u in range(-512, 512):
                    bad += img[pic + (v & 255) * 256 + (u & 255)] != env[src[fr] + (v & 127) * 256 + (u & 127)]
            assert not bad, f"material {k}, frame {fr}: {bad} texels differ"
    for phase in (0, 0x5556, 0x8000, 0xAAAA, 0xFF00):
        assert frames * phase >> 16 < frames
    print(f"track {n}: {len(windows)} window materials of the cars read the reflection: {frames} frame(s), "
          f"every texel of u, v -512..511 equals the original's (wrap 128)")


main()
