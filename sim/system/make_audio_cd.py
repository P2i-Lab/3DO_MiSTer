#!/usr/bin/env python3
"""Writes a test audio CD (CUE + BIN, CD-DA little-endian as in BIN dumps): 3 tracks of 20 s
of pure tones, a different frequency on each channel, so that the sound recorded by the
simulation tells the track, the channel order and the byte order.
    track 1: left 440 Hz, right 660 Hz
    track 2: left 880 Hz, right 1320 Hz
    track 3: left 1760 Hz, right 2640 Hz
usage: make_audio_cd.py FOLDER"""
import math
import os
import struct
import sys

TONES = [(440, 660), (880, 1320), (1760, 2640)]
SECONDS = 20
RATE = 44100


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cue = ['FILE "tones.bin" BINARY']
    with open(os.path.join(out, "tones.bin"), "wb") as f:
        for t, (fl, fr) in enumerate(TONES):
            start = t * SECONDS * 75
            cue += ["  TRACK %02d AUDIO" % (t + 1),
                    "    INDEX 01 %02d:%02d:%02d" % (start // 75 // 60, start // 75 % 60, start % 75)]
            n = SECONDS * RATE
            buf = bytearray()
            for i in range(n):
                l = int(12000 * math.sin(2 * math.pi * fl * i / RATE))
                r = int(6000 * math.sin(2 * math.pi * fr * i / RATE))
                buf += struct.pack("<hh", l, r)
            f.write(buf)
    with open(os.path.join(out, "tones.cue"), "w") as f:
        f.write("\n".join(cue) + "\n")


if __name__ == "__main__":
    main()
