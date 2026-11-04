#!/usr/bin/env python3
"""Decode a 3DO NVRAM image, such as a 32 KB .sav file of the MiSTer 3DO core.

The NVRAM holds a Portfolio "linked memory" file system (see lmadm.c/format.c in
trapexit/portfolio_os): a disc label (132 bytes) followed by a chain of blocks, each
starting with a 20-byte header (fingerprint, forward link, backward link, size,
header size). Everything is big-endian.

Usage: nvram_dump.py <file.sav> [...]
Prints the volume label, every block in physical order, the files with their size,
the free space, and the problems found. Exit status: 0 if the volume is consistent.
"""
import struct
import sys

LABEL_SIZE = 0x84
BLOCK_HDR = 0x14
FINGERPRINTS = {0x855A02B6: "ANCHOR", 0x7AA565BD: "FREE", 0xBE4F32A6: "FILE"}


def printable(raw):
    text = raw.split(b"\0")[0]
    return "".join(chr(c) if 32 <= c < 127 else "." for c in text)


def dump(path):
    data = open(path, "rb").read()
    print(f"== {path}: {len(data)} bytes")
    problems = []
    if len(data) < LABEL_SIZE:
        print("   too small for a 3DO NVRAM image")
        return 1
    nonzero = sum(1 for b in data if b)
    print(f"   non-zero bytes: {nonzero}")

    rtype, sync, version, flags = data[0], data[1:6], data[6], data[7]
    commentary, volume_id = printable(data[0x08:0x28]), printable(data[0x28:0x48])
    (vol_uid, block_size, block_count, root_uid, root_blocks, root_block_size,
     root_last_avatar) = struct.unpack(">7I", data[0x48:0x64])
    avatars = struct.unpack(">8I", data[0x64:0x84])
    print(f"   label: type={rtype} sync={sync!r} version={version} flags={flags} "
          f"id='{volume_id}' comment='{commentary}'")
    print(f"   volume: uid={vol_uid:#010x} block_size={block_size} block_count={block_count} "
          f"root_uid={root_uid:#010x} first_avatar={avatars[0]:#x}")
    if rtype != 1 or sync != b"ZZZZZ" or version != 2:
        print("   NOT FORMATTED: no linked-memory volume label (the OS cannot use this NVRAM)")
        return 1
    if block_size != 1 or block_count != len(data):
        problems.append(f"block size/count {block_size}/{block_count} do not match a {len(data)}-byte image")

    # Physical walk: blocks are contiguous from the end of the label
    offset, files, free, blocks = LABEL_SIZE, [], 0, {}
    while offset < len(data):
        if offset + BLOCK_HDR > len(data):
            problems.append(f"truncated block header at {offset:#06x}")
            break
        fp, flink, blink, count, hdr = struct.unpack(">5I", data[offset:offset + BLOCK_HDR])
        kind = FINGERPRINTS.get(fp, f"?{fp:08X}")
        blocks[offset] = (kind, flink, blink, count)
        extra = ""
        if kind == "FILE":
            if offset + BLOCK_HDR + 44 <= len(data):
                byte_count, uid, ftype = struct.unpack(">3I", data[offset + BLOCK_HDR:offset + BLOCK_HDR + 12])
                name = printable(data[offset + BLOCK_HDR + 12:offset + BLOCK_HDR + 44])
                extra = f" name='{name}' bytes={byte_count} type={ftype:#010x}"
                files.append((name, byte_count, count))
        elif kind == "FREE":
            free += count
        print(f"   {offset:#06x} {kind:7} size={count:#06x} header={hdr:#x} next={flink:#06x} prev={blink:#06x}{extra}")
        if kind.startswith("?"):
            problems.append(f"unknown fingerprint at {offset:#06x}: the chain is corrupted")
            break
        if count < BLOCK_HDR or offset + count > len(data):
            problems.append(f"bad block size {count:#x} at {offset:#06x}")
            break
        offset += count

    # Link check: follow the forward links from the anchor and come back to it
    start = avatars[0]
    if start in blocks:
        seen, cur = [], start
        while cur in blocks and cur not in seen:
            seen.append(cur)
            nxt = blocks[cur][1]
            if nxt in blocks and blocks[nxt][2] != cur:
                problems.append(f"backward link of {nxt:#06x} does not point to {cur:#06x}")
            cur = nxt
        if cur != start:
            problems.append(f"forward chain from {start:#06x} does not loop back (stops at {cur:#06x})")
        if len(seen) != len(blocks):
            problems.append(f"{len(blocks) - len(seen)} block(s) are not reachable from the anchor")
    else:
        problems.append(f"first avatar {start:#06x} is not a block start")

    print(f"   files: {len(files)}, free space: {free} bytes ({free * 100 // max(len(data), 1)} %)")
    for problem in problems:
        print(f"   PROBLEM: {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(max(dump(p) for p in sys.argv[1:]))
