"""png_sheet.py OUT.png COLS SCALE frame_*.png : contact sheet of the PNG pictures of a run (8-bit RGB/RGBA,
any filter), each shrunk by SCALE (nearest pixel) and labelled with the frame number of its file name."""
import re
import struct
import sys
import zlib

FONT = {  # 3x5 digits
    "0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
    "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
    "8": "111101111101111", "9": "111101111001111",
}


def read_png(path):
    d = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(d):
        n = struct.unpack(">I", d[pos:pos + 4])[0]
        t = d[pos + 4:pos + 8]
        c = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", c[:10])
        elif t == b"IDAT":
            idat += c
        pos += 12 + n
    if depth != 8 or ctype not in (2, 6):
        sys.exit("%s: unsupported PNG (depth %d type %d)" % (path, depth, ctype))
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def main():
    out, cols, scale, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
    tiles = []
    for f in files:
        w, h, bpp, rows = read_png(f)
        tw, th = w // scale, h // scale
        t = [bytearray(tw * 3) for _ in range(th)]
        for y in range(th):
            src = rows[y * scale]
            dst = t[y]
            for x in range(tw):
                o = x * scale * bpp
                dst[3 * x:3 * x + 3] = src[o:o + 3]
        label = re.findall(r"\d+", f.rsplit("/", 1)[-1])
        label = label[-1].lstrip("0") or "0" if label else ""
        for k, ch in enumerate(label):
            for py in range(5):
                for px in range(3):
                    on = FONT[ch][py * 3 + px] == "1"
                    for dy in range(2):
                        for dx in range(2):
                            yy, xx = 2 + py * 2 + dy, 2 + k * 8 + px * 2 + dx
                            if yy < th and xx < tw:
                                t[yy][3 * xx:3 * xx + 3] = b"\xff\xff\x00" if on else b"\x00\x00\x00"
        tiles.append((tw, th, t))
    tw = max(t[0] for t in tiles)
    th = max(t[1] for t in tiles)
    nrows = (len(tiles) + cols - 1) // cols
    W, H = cols * (tw + 2), nrows * (th + 2)
    img = [bytearray(W * 3) for _ in range(H)]
    for i, (w, h, t) in enumerate(tiles):
        ox, oy = (i % cols) * (tw + 2), (i // cols) * (th + 2)
        for y in range(h):
            img[oy + y][3 * ox:3 * (ox + w)] = t[y]
    raw = b"".join(b"\x00" + bytes(r) for r in img)

    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(out, "wb").write(png)
    print("%s: %d tiles, %dx%d" % (out, len(tiles), W, H))


main()
