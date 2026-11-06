"""The menus' music in the simulation (firmware built with -DS2_SIM_MENUMUSIC=1, tools/fw.sh): a made-up CD image whose frames carry their own place, and a
check of the sound the real mixer made from it (SIM_WAV).

  python -I tools/musicwav.py make <folder>        GAME.GOG and GAME.DAT into <folder> (inside the repository's build/; the ZIP of the game goes there too, by the caller)
  python -I tools/musicwav.py check <wav>          decode the places the sound plays and say where it is silent, where it jumps

The image: track 2 is 3000 sectors (40 s), track 3 100 sectors. In track 2 the frame i is left = (i mod 50000) * 65535 / 50000 - 32768 (a ramp that starts over every
50,000 frames: not the ring's 65,536, or a repeat of the old ring would look like the song going on), right = i div 50000. The mixer plays the ring at VOL 0x4000 of
0x8000 (firmware/music.c), so the sound has half of that, resampled from 44.1 to 48 kHz by a straight line between two frames (STEP 60211 / 65536: 0.91875 frames a
sample). From a sample's left value the place in the song modulo 50,000 follows (to 1.5 frames); in a stretch of sound the place goes on by 0.91875 a sample. The check
follows the place sample by sample (a sample off the line, as the one between the last frame of a lap and the first of the next is, is skipped) and reports
  - the stretches of sound and the silences between them (a hold of the firmware is a silence; the song must go on from where it stood, one sector (588 frames) back at the most)
  - every jump of the place inside a stretch (the ring ran dry and the mixer played the old content again, or a read landed in the wrong place): there must be none.
Exit code 0 if the sound is one song with nothing but the holds' silences, and at least one hold was seen (the test is no use without it).
"""
import struct
import sys
import wave

SECTOR = 2352
TRACK2, TRACK2_SECTORS, TRACK3_SECTORS = 150, 3000, 100
STEP = 60211 / 65536.0
FRAMES_PER_SECTOR = SECTOR // 4
LAP = 50000


def make(folder):
    total = TRACK2 + TRACK2_SECTORS + TRACK3_SECTORS
    with open(folder + "/GAME.GOG", "wb") as f:
        f.write(b"\0" * (TRACK2 * SECTOR))                      # track 1 (data) and the pregap
        for i in range(TRACK2_SECTORS * FRAMES_PER_SECTOR):
            f.write(struct.pack("<hh", (i % LAP) * 65535 // LAP - 32768, i // LAP))
        f.write(b"\x11\0\x11\0" * (TRACK3_SECTORS * FRAMES_PER_SECTOR))

    def msf(s):
        return "%02d:%02d:%02d" % (s // 75 // 60, s // 75 % 60, s % 75)
    cue = ("FILE \"GAME.GOG\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
           "  TRACK 02 AUDIO\r\n    INDEX 01 %s\r\n  TRACK 03 AUDIO\r\n    INDEX 01 %s\r\n" % (msf(TRACK2), msf(TRACK2 + TRACK2_SECTORS)))
    open(folder + "/GAME.DAT", "w", newline="").write(cue)
    print("%s: GAME.GOG %d bytes (%d sectors), GAME.DAT" % (folder, total * SECTOR, total))


def wrap(x):
    """x modulo LAP into (-LAP/2, LAP/2]"""
    x = (x + LAP / 2.0) % LAP - LAP / 2.0
    return x


def check(path):
    w = wave.open(path, "rb")
    assert w.getnchannels() == 2 and w.getsampwidth() == 2 and w.getframerate() == 48000
    raw = w.readframes(w.getnframes())
    n = len(raw) // 4
    samples = struct.unpack("<%dh" % (2 * n), raw[:4 * n])
    rate = 48000.0
    # the place of each sample that is sound, modulo LAP
    m = [None] * n
    for j in range(n):
        l, r = samples[2 * j], samples[2 * j + 1]
        if l == 0 and r == 0:
            continue
        m[j] = (2 * l + 32768) * LAP / 65535.0
    # follow the place: a sample on the line from the last accepted one is accepted; 12 in a row off it are a jump (the line starts again there)
    stretches = []                               # [first j, last j, place at the start, place at the end] (places modulo LAP)
    jumps = []
    cur = None
    last_j = None
    off_run = []
    for j in range(n):
        if m[j] is None:
            continue
        if cur is None or j - last_j > 100:      # silence of more than 2 ms: a new stretch
            if cur is not None:
                stretches.append(cur)
            cur = [j, j, m[j], m[j]]
            last_j = j
            off_run = []
            continue
        pred = cur[3] + STEP * (j - last_j)
        diff = wrap(m[j] - pred)
        if abs(diff) <= 12:
            cur[1], cur[3] = j, m[j]
            last_j = j
            off_run = []
        else:
            off_run.append((j, m[j]))
            if len(off_run) >= 12:               # a jump: the new line begins at the first of these samples
                j0, m0 = off_run[0]
                jumps.append((j0, wrap(m0 - pred)))
                cur[1], cur[3] = j0, m0
                last_j = j0
                off_run = []
    if cur is not None:
        stretches.append(cur)
    print("%s: %d samples (%.2f s), %d stretches of sound" % (path, n, n / rate, len(stretches)))
    holds = 0
    ok = True
    for i, (a, b, ma, mb) in enumerate(stretches):
        line = "  stretch %d: %.3f s to %.3f s" % (i + 1, a / rate, b / rate)
        if i:
            gap_ms = (a - stretches[i - 1][1]) * 1000.0 / rate
            back = wrap(stretches[i - 1][3] - ma)
            line += ", silence %.0f ms before it, the song goes on %.0f frames before where it stood (a sector is %d)" % (gap_ms, back, FRAMES_PER_SECTOR)
            holds += 1
            if back < -12 or back > FRAMES_PER_SECTOR + 12:
                ok = False
                line += "  <-- WRONG PLACE"
        print(line)
    for j, dp in jumps:
        ok = False
        print("  JUMP of the place by %.0f frames at %.3f s (the ring ran dry, or a read landed wrong)" % (dp, j / rate))
    if holds == 0:
        ok = False
        print("  no hold was seen: the test did not test anything")
    print("musicwav: %s" % ("one song with %d hold(s), no jump inside a stretch" % holds if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "make":
        make(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        sys.exit(check(sys.argv[2]))
    else:
        sys.exit(__doc__)
