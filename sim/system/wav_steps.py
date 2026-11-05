#!/usr/bin/env python3
"""Decodes a recording of the make_steps_cd.py disc: for every 0.25 s of the 16-bit stereo WAV, the
step heard on the left (strongest of the step frequencies 200 + 25 k Hz) and whether the right channel
holds twice that frequency. Continuous playback at the right speed reads k = 0, 0, 1, 1, 2, 2, ...;
a skip shows as a jump, a wrong speed as steps lasting more or less than 0.5 s.
usage: wav_steps.py audio.wav [--swap]   (--swap: read the channels the other way round)"""
import math
import struct
import sys


def goertzel(x, rate, f):
    k = 2 * math.cos(2 * math.pi * f / rate)
    s1 = s2 = 0.0
    for v in x:
        s1, s2 = v + k * s1 - s2, s1
    return math.sqrt(max(s1 * s1 + s2 * s2 - k * s1 * s2, 0)) / len(x)


def main():
    d = open(sys.argv[1], "rb").read()
    rate = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    n = len(pcm) // 4
    win = rate // 4
    prev = None
    for s in range(0, n - win + 1, win):
        a = s + win // 4               # analyse the middle half of the window
        m = win // 2
        left = [struct.unpack_from("<h", pcm, 4 * (a + i))[0] for i in range(m)]
        right = [struct.unpack_from("<h", pcm, 4 * (a + i) + 2)[0] for i in range(m)]
        if "--swap" in sys.argv[2:]:
            left, right = right, left
        rms = math.sqrt(sum(v * v for v in left) / m)
        if rms < 300:
            continue
        mags = [(goertzel(left, rate, 200 + 25 * k), k) for k in range(60)]
        mag, k = max(mags)
        rmag = goertzel(right, rate, 2 * (200 + 25 * k))
        note = ""
        if prev is not None and k not in (prev, prev + 1):
            note = "  <-- jump from step %d" % prev
        print("%6.2f s  step %2d (%4d Hz)  left %5.0f  right x2 %5.0f%s" % (s / rate, k, 200 + 25 * k, mag, rmag, note))
        prev = k


if __name__ == "__main__":
    main()
