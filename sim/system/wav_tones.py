#!/usr/bin/env python3
"""Tells, for each half second of a 16-bit stereo WAV recorded by the simulation, the level of
each channel and which test tone of make_audio_cd.py it holds (Goertzel filters, no numpy).
usage: wav_tones.py audio.wav"""
import math
import struct
import sys

FREQS = [440, 660, 880, 1320, 1760, 2640]


def goertzel(samples, rate, f):
    k = 2 * math.cos(2 * math.pi * f / rate)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + k * s1 - s2, s1
    return math.sqrt(max(s1 * s1 + s2 * s2 - k * s1 * s2, 0)) / len(samples)


def main():
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    rate = struct.unpack("<I", data[24:28])[0]
    pcm = data[44:]
    n = len(pcm) // 4
    left = [struct.unpack_from("<h", pcm, 4 * i)[0] for i in range(n)]
    right = [struct.unpack_from("<h", pcm, 4 * i + 2)[0] for i in range(n)]
    step = rate // 2
    print("time    left: rms  tone      right: rms  tone")
    for start in range(0, n - step + 1, step):
        line = "%5.1f s" % (start / rate)
        for ch in (left, right):
            seg = ch[start:start + step]
            rms = math.sqrt(sum(x * x for x in seg) / len(seg))
            mags = [(goertzel(seg, rate, f), f) for f in FREQS]
            best, f = max(mags)
            tone = "%d Hz" % f if rms > 200 and best > 0.25 * rms else "-"
            line += "   %8.0f  %-8s" % (rms, tone)
        print(line)


if __name__ == "__main__":
    main()
