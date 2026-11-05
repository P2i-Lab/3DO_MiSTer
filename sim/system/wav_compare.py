#!/usr/bin/env python3
"""Compares two 16-bit stereo 44.1 kHz recordings of the same moment of a game (e.g. the simulation and
an emulator): finds the time offset that best aligns them (correlation of 10 ms loudness envelopes),
then correlates the 1 ms loudness envelopes of each channel pair. Same stereo orientation:
LL and RR above LR and RL. No numpy needed.
usage: wav_compare.py A.wav B.wav [from_s to_s]   (window in A's time, default: all)"""
import math
import struct
import sys


def load(path):
    d = open(path, "rb").read()
    rate = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    n = len(pcm) // 4
    v = struct.unpack("<%dh" % (2 * n), pcm[:4 * n])
    return rate, v[0::2], v[1::2]


def envelope(x, step):
    out = []
    for i in range(0, len(x) - step + 1, step):
        s = 0
        for v in x[i:i + step]:
            s += v * v
        out.append(math.sqrt(s / step))
    return out


def corr(a, b):
    n = min(len(a), len(b))
    if n < 10:
        return 0.0
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    sab = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    saa = sum((x - ma) ** 2 for x in a)
    sbb = sum((y - mb) ** 2 for y in b)
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def main():
    ra, la, rha = load(sys.argv[1])
    rb, lb, rhb = load(sys.argv[2])
    f0 = float(sys.argv[3]) if len(sys.argv) > 4 else 0.0
    f1 = float(sys.argv[4]) if len(sys.argv) > 4 else len(la) / ra
    # 10 ms mono envelopes, offset search over +-3 s
    ea = envelope([x + y for x, y in zip(la, rha)], ra // 100)
    eb = envelope([x + y for x, y in zip(lb, rhb)], rb // 100)
    a0, a1 = int(f0 * 100), min(int(f1 * 100), len(ea))
    best = (-2.0, 0)
    for off in range(-300, 301):
        b0, b1 = a0 + off, a1 + off
        if b0 < 0 or b1 > len(eb):
            continue
        c = corr(ea[a0:a1], eb[b0:b1])
        best = max(best, (c, off))
    c, off = best
    print("best offset B - A = %+.2f s (envelope correlation %.3f)" % (off / 100.0, c))
    # 1 ms per-channel envelopes over the window, refined offset +-10 ms
    step = ra // 1000
    s0, s1 = int(f0 * ra), min(int(f1 * ra), len(la))
    A = [envelope(la[s0:s1], step), envelope(rha[s0:s1], step)]
    bestf = (-2.0, 0)
    for d in range(-10, 11):
        t0 = s0 + (off * 10 + d) * step
        t1 = t0 + (s1 - s0)
        if t0 < 0 or t1 > len(lb):
            continue
        m = corr(envelope([x + y for x, y in zip(la[s0:s1], rha[s0:s1])], step * 10),
                 envelope([x + y for x, y in zip(lb[t0:t1], rhb[t0:t1])], step * 10))
        bestf = max(bestf, (m, d))
    t0 = s0 + (off * 10 + bestf[1]) * step
    t1 = t0 + (s1 - s0)
    if t0 < 0 or t1 > len(lb):
        print("window out of range in B")
        return
    B = [envelope(lb[t0:t1], step), envelope(rhb[t0:t1], step)]
    names = "LR"
    for i in range(2):
        print("  A.%s vs B.L %.3f   vs B.R %.3f" % (names[i], corr(A[i], B[0]), corr(A[i], B[1])))
    ll, rr, lr, rl = corr(A[0], B[0]), corr(A[1], B[1]), corr(A[0], B[1]), corr(A[1], B[0])
    print("same orientation score (LL+RR) - (LR+RL) = %+.3f" % ((ll + rr) - (lr + rl)))


if __name__ == "__main__":
    main()
