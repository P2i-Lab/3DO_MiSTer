#!/usr/bin/env python3
"""Writes a test audio CD (CUE + BIN, CD-DA little-endian as in BIN dumps) whose tone changes every
half second, so that a recording tells whether the player plays the disc continuously, at the right
speed, without skips: one 30 s track, step k (from 0) lasts 0.5 s,
    left  = 200 + 25 * k Hz
    right = 2 * left
The phase is kept continuous across steps. wav_steps.py decodes a recording back into steps.
usage: make_steps_cd.py FOLDER [--be]   (--be: big-endian samples, for emulators that swap BIN audio)"""
import math
import os
import struct
import sys

SECONDS = 30
RATE = 44100
STEP = RATE // 2


def freq(k):
    return 200 + 25 * k


def main():
    out = sys.argv[1]
    be = "--be" in sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    pl = pr = 0.0
    buf = bytearray()
    for i in range(SECONDS * RATE):
        f = freq(i // STEP)
        pl += 2 * math.pi * f / RATE
        pr += 2 * math.pi * 2 * f / RATE
        buf += struct.pack(">hh" if be else "<hh", int(12000 * math.sin(pl)), int(6000 * math.sin(pr)))
    with open(os.path.join(out, "steps.bin"), "wb") as fo:
        fo.write(buf)
    with open(os.path.join(out, "steps.cue"), "w") as fo:
        fo.write('FILE "steps.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n')


if __name__ == "__main__":
    main()
