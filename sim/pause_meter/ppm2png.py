#!/usr/bin/env python3
"""ppm2png.py IN.ppm OUT.png [SCALE]: converts an ASCII PPM (P3) to PNG, enlarged SCALE times."""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    tokens = open(src).read().split()
    assert tokens[0] == "P3"
    w, h, maxval = int(tokens[1]), int(tokens[2]), int(tokens[3])
    vals = [int(v) * 255 // maxval for v in tokens[4:4 + w * h * 3]]
    rows = []
    for y in range(h):
        line = bytearray()
        for x in range(w):
            line += bytes(vals[(y * w + x) * 3:(y * w + x) * 3 + 3]) * scale
        rows += [b"\x00" + bytes(line)] * scale
    raw = b"".join(rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)


if __name__ == "__main__":
    main()
