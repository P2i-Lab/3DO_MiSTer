#!/usr/bin/env python3
"""Print the identifiers the 3DO core uses to recognise a disc (per-game settings in 3DO.sv).

usage: disc_id.py IMAGE|FOLDER [...]   (.cue, .iso, .bin, .chd)
       A folder is searched with its subfolders for .cue, .iso and .chd images.

3DO.sv reads, from the disc information Main_MiSTer sends at load time:
  id  = the volume unique identifier of the Opera label (sector 0, user data offset 0x48,
        big-endian), used to pick the VRAM blocks kept in FPGA memory (BRAM4/2/1_OFFS)
        and the DSP pause;
  msf = the lead-out position (binary minutes, seconds, frames, then 0), used when two
        discs share an id (not computed for CHD images).
Example line for 3DO.sv:
  if (id == 32'h031BED09) {BRAM4_OFFS,BRAM2_OFFS,BRAM1_OFFS} <= {4'h8,4'h6,4'hC};//Quarantine (US)
"""
import os
import re
import struct
import sys
import zlib

try:
    import lzma
except ImportError:         # not built into every Python (MiSTer's Linux?): only cdlz CHDs need it
    lzma = None

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def sector0(path, sector_size=None, first=0):
    """User data of the first sector of a data track that starts `first` sectors into the file."""
    with open(path, "rb") as f:
        if sector_size is None:
            sector_size = 2352 if f.read(12) == SYNC else 2048
        f.seek(first * sector_size)
        head = f.read(sector_size)
    if sector_size == 2048:
        return head[:2048], 2048
    mode = head[15]
    return (head[24:24 + 2048] if mode == 2 else head[16:16 + 2048]), 2352


def parse_cue(path):
    """Files of a CUE sheet, each with its first track mode and the sector of that track's
    INDEX 01; the data track comes first."""
    base = os.path.dirname(path)
    files = []
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            m = re.match(r'FILE\s+"?(.*?)"?\s+(BINARY|MOTOROLA|WAVE)\s*$', line, re.I)
            if m:
                files.append([os.path.join(base, m.group(1)), None, None])
                continue
            m = re.match(r"TRACK\s+\d+\s+(\S+)", line, re.I)
            if m and files and files[-1][1] is None:
                files[-1][1] = m.group(1).upper()
            m = re.match(r"INDEX\s+0*1\s+(\d+):(\d+):(\d+)", line, re.I)
            if m and files and files[-1][2] is None:
                files[-1][2] = (int(m.group(1)) * 60 + int(m.group(2))) * 75 + int(m.group(3))
    return files


# CHD images (MAME "compressed hunks of data", version 5): only what is needed to read the
# first data sector. Format as described by MAME's src/lib/util/chd.cpp, chdcodec.cpp and
# huffman.cpp.
CD_FRAME = 2448                                   # a CD frame in a CHD: sector + 96-byte subcode
CD_SECTOR = 2352
HUNK_NONE, HUNK_SELF, HUNK_PARENT = 4, 5, 6       # map types 0-3 = compressors 0-3
RLE_SMALL, RLE_LARGE, SELF_0, SELF_1, PARENT_SELF, PARENT_0, PARENT_1 = range(7, 14)


class Bits:
    """Reads a byte string as a stream of bits, most significant bit first."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def peek(self, n):
        i = self.pos >> 3
        word = int.from_bytes(self.data[i:i + 8].ljust(8, b"\x00"), "big")
        return (word >> (64 - (self.pos & 7) - n)) & ((1 << n) - 1)

    def read(self, n):
        v = self.peek(n)
        self.pos += n
        return v


def huffman_lookup(bits):
    """Reads the code lengths of the map's Huffman code (16 symbols, at most 8 bits, run-length
    coded) and returns the table that maps the next 8 bits to (symbol, code length)."""
    lengths = []
    while len(lengths) < 16:
        n = bits.read(4)
        if n != 1:
            lengths.append(n)
        else:
            n = bits.read(4)
            if n == 1:
                lengths.append(1)
            else:
                lengths += [n] * (bits.read(4) + 3)
    if len(lengths) != 16 or max(lengths) > 8:
        raise ValueError("invalid CHD map (Huffman code)")
    start = [0] * 33                              # canonical codes, longest codes first
    for n in lengths:
        start[n] += 1
    code = 0
    for n in range(32, 0, -1):
        following = (code + start[n]) >> 1
        if n != 1 and following * 2 != code + start[n]:
            raise ValueError("invalid CHD map (Huffman code)")
        start[n], code = code, following
    table = [(0, 8)] * 256
    for symbol, n in enumerate(lengths):
        if n:
            first = start[n] << (8 - n)
            start[n] += 1
            table[first:first + (1 << (8 - n))] = [(symbol, n)] * (1 << (8 - n))
    return table


def huffman_decode(bits, table):
    symbol, n = table[bits.peek(8)]
    bits.pos += n
    return symbol


def chd_header(f):
    head = f.read(124)
    if head[:8] != b"MComprHD":
        raise ValueError("not a CHD file")
    version = struct.unpack(">I", head[12:16])[0]
    if version != 5:
        raise ValueError(f"CHD version {version}, only version 5 is read (chdman 0.146 or later)")
    hdr = {"codecs": [head[16 + 4 * i:20 + 4 * i] for i in range(4)]}
    hdr["logical"], hdr["map"], hdr["meta"] = struct.unpack(">QQQ", head[32:56])
    hdr["hunkbytes"], hdr["unitbytes"] = struct.unpack(">II", head[56:64])
    return hdr


def chd_tracks(f, hdr):
    """Track descriptions of the CD metadata (TRACK, TYPE, FRAMES, PREGAP, PGTYPE...)."""
    tracks, offset = [], hdr["meta"]
    for _ in range(1000):
        if not offset:
            break
        f.seek(offset)
        h = f.read(16)
        tag, length, offset = h[0:4], int.from_bytes(h[5:8], "big"), int.from_bytes(h[8:16], "big")
        if tag in (b"CHT2", b"CHTR"):
            text = f.read(length).split(b"\x00")[0].decode("latin-1")
            tracks.append(dict(re.findall(r"(\w+):(\S+)", text)))
    tracks.sort(key=lambda t: int(t.get("TRACK", "0")))
    return tracks


def chd_map(f, hdr, last):
    """(type, offset, length) of hunks 0 to `last`. In a compressed map, the types of all the
    hunks come first (Huffman coded, with repeats), then the offsets and lengths."""
    hb, ub = hdr["hunkbytes"], hdr["unitbytes"]
    f.seek(hdr["map"])
    if hdr["codecs"][0] == b"\x00" * 4:          # uncompressed CHD: hunk numbers, 0 = zeros
        raw = f.read(4 * (last + 1))
        return [(HUNK_NONE, struct.unpack(">I", raw[4 * h:4 * h + 4])[0] * hb, hb)
                for h in range(last + 1)]
    raw = f.read(16)
    size = struct.unpack(">I", raw[0:4])[0]
    offset = int.from_bytes(raw[4:10], "big")
    lengthbits, selfbits, parentbits = raw[12], raw[13], raw[14]
    bits = Bits(f.read(size))
    table = huffman_lookup(bits)
    types, last_type, repeat = [], 0, 0
    for _ in range((hdr["logical"] + hb - 1) // hb):
        if repeat:
            repeat -= 1
        else:
            v = huffman_decode(bits, table)
            if v == RLE_SMALL:
                repeat = 2 + huffman_decode(bits, table)
            elif v == RLE_LARGE:
                repeat = 2 + 16 + (huffman_decode(bits, table) << 4)
                repeat += huffman_decode(bits, table)
            else:
                last_type = v
        types.append(last_type)
    hunks, last_self, last_parent = [], 0, 0
    for h in range(last + 1):
        t, start, length = types[h], offset, 0
        if t <= HUNK_NONE:
            length = bits.read(lengthbits) if t < HUNK_NONE else hb
            offset += length
            bits.read(16)                         # CRC of the hunk
        elif t == HUNK_SELF:
            start = last_self = bits.read(selfbits)
        elif t == HUNK_PARENT:
            start = last_parent = bits.read(parentbits)
        elif t in (SELF_0, SELF_1):
            last_self += t == SELF_1
            t, start = HUNK_SELF, last_self
        elif t == PARENT_SELF:
            t, start = HUNK_PARENT, h * hb // ub
            last_parent = start
        elif t in (PARENT_0, PARENT_1):
            if t == PARENT_1:
                last_parent += hb // ub
            t, start = HUNK_PARENT, last_parent
        else:
            raise ValueError("invalid CHD map (hunk type)")
        hunks.append((t, start, length))
    return hunks


def chd_hunk(f, hdr, hunks, h):
    """Sector bytes (2352 per frame, without the subcode) of hunk h."""
    t, start, length = hunks[h]
    if t == HUNK_SELF:
        return chd_hunk(f, hdr, hunks, start)     # copy of an earlier hunk
    if t == HUNK_PARENT:
        raise ValueError("this CHD only stores its differences with a parent CHD")
    hb = hdr["hunkbytes"]
    frames = hb // CD_FRAME
    if t == HUNK_NONE:
        f.seek(start)
        raw = f.read(hb) if start else bytes(hb)
        return b"".join(raw[i * CD_FRAME:i * CD_FRAME + CD_SECTOR] for i in range(frames))
    codec = hdr["codecs"][t]
    f.seek(start)
    src = f.read(length)
    # CD codecs: a bit per frame whose sync and ECC were removed, the length of the
    # compressed sectors, the sectors of all the frames, then their subcode.
    flags = (frames + 7) // 8
    size_bytes = 2 if hb < 65536 else 3
    base = int.from_bytes(src[flags:flags + size_bytes], "big")
    data = src[flags + size_bytes:flags + size_bytes + base]
    if codec == b"cdzl":
        return zlib.decompressobj(-15).decompress(data, frames * CD_SECTOR)
    if codec == b"cdlz":
        if lzma is None:
            raise ValueError("first data sector compressed with LZMA, and this Python has no "
                             "lzma module")
        lzma1 = {"id": lzma.FILTER_LZMA1, "dict_size": 1 << 20, "lc": 3, "lp": 0, "pb": 2}
        return lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=[lzma1]).decompress(
            data, frames * CD_SECTOR)
    raise ValueError(f"first data sector compressed with {codec.decode('latin-1')!r}, "
                     "not supported here")


def chd_sector0(path):
    """User data of the first sector of track 1 of a CD CHD."""
    with open(path, "rb") as f:
        hdr = chd_header(f)
        if hdr["unitbytes"] != CD_FRAME or hdr["hunkbytes"] % CD_FRAME:
            raise ValueError("not a CD image")
        tracks = chd_tracks(f, hdr)
        track = tracks[0] if tracks else {}
        frame = int(track.get("PREGAP", "0")) if track.get("PGTYPE", "").startswith("V") else 0
        per_hunk = hdr["hunkbytes"] // CD_FRAME
        hunks = chd_map(f, hdr, frame // per_hunk)
        data = chd_hunk(f, hdr, hunks, frame // per_hunk)
    sector = data[(frame % per_hunk) * CD_SECTOR:][:CD_SECTOR]
    offsets = (16, 24, 0) if "RAW" in track.get("TYPE", "MODE1_RAW") else (0, 16, 24)
    for o in offsets:
        if sector[o] == 1 and sector[o + 1:o + 6] == b"\x5a" * 5:
            return sector[o:o + 2048]
    return sector[offsets[0]:offsets[0] + 2048]


def describe(path):
    ext = os.path.splitext(path)[1].lower()
    total = None
    if ext == ".chd":
        data = chd_sector0(path)
    elif ext == ".cue":
        files = parse_cue(path)
        if not files:
            return f"{path}: no FILE in the CUE"
        data_file, mode, first = files[0]
        size = 2048 if mode == "MODE1/2048" else 2352
        data, size = sector0(data_file, size, first or 0)
        total = 0
        for fpath, fmode, _ in files:
            fsize = 2048 if fmode == "MODE1/2048" else 2352
            total += (os.path.getsize(fpath) + fsize - 1) // fsize
        total -= first or 0
    else:
        data, size = sector0(path)
        total = (os.path.getsize(path) + size - 1) // size
    if data[0] != 1 or data[1:6] != b"\x5a" * 5:
        return f"{path}: sector 0 is not an Opera label (not a 3DO data disc?)"
    vol_id = struct.unpack(">I", data[0x48:0x4C])[0]
    name = data[0x28:0x48].split(b"\x00")[0].decode("latin-1", "replace")
    text = f"{os.path.basename(path)}: volume \"{name}\"\n  id  = 32'h{vol_id:08X}"
    if total is not None:
        lead = total + 150
        m, s, f = lead // 75 // 60, (lead // 75) % 60, lead % 75
        msf = (m << 24) | (s << 16) | (f << 8)
        text += f"\n  msf = 32'h{msf:08X}  (lead-out {m:02d}:{s:02d}:{f:02d}, {total} sectors)"
    return text


def images(arg):
    """The image itself, or the images found in a folder and its subfolders (a CUE stands for
    its BIN files, which are not listed again)."""
    if not os.path.isdir(arg):
        return [arg]
    found = []
    for root, dirs, names in os.walk(arg):
        dirs.sort()
        found += [os.path.join(root, n) for n in sorted(names)
                  if os.path.splitext(n)[1].lower() in (".cue", ".iso", ".chd")]
    if not found:
        print(f"{arg}: no .cue, .iso or .chd image in this folder")
    return found


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    errors = (OSError, ValueError, IndexError, zlib.error) + ((lzma.LZMAError,) if lzma else ())
    for arg in sys.argv[1:]:
        for p in images(arg):
            try:
                print(describe(p))
            except errors as e:
                print(f"{p}: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
