#!/usr/bin/env python3
"""Print the identifiers the 3DO core uses to recognise a disc (per-game settings in 3DO.sv).

usage: disc_id.py IMAGE [IMAGE...]      (.cue, .iso, .bin; not .chd)

3DO.sv reads, from the disc information Main_MiSTer sends at load time:
  id  = the volume unique identifier of the Opera label (sector 0, user data offset 0x48,
        big-endian), used to pick the VRAM blocks kept in FPGA memory (BRAM4/2/1_OFFS)
        and the DSP pause;
  msf = the lead-out position (binary minutes, seconds, frames, then 0), used when two
        discs share an id.
Example line for 3DO.sv:
  if (id == 32'h031BED09) {BRAM4_OFFS,BRAM2_OFFS,BRAM1_OFFS} <= {4'h8,4'h6,4'hC};//Quarantine (US)
"""
import os
import re
import struct
import sys

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def sector0(path, sector_size=None):
    """User data of sector 0 of a data track file."""
    with open(path, "rb") as f:
        head = f.read(2352)
    if sector_size is None:
        sector_size = 2352 if head[:12] == SYNC else 2048
    if sector_size == 2048:
        return head[:2048], 2048
    mode = head[15]
    return (head[24:24 + 2048] if mode == 2 else head[16:16 + 2048]), 2352


def parse_cue(path):
    """Files of a CUE sheet, each with its first track mode; the data track comes first."""
    base = os.path.dirname(path)
    files = []
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            m = re.match(r'FILE\s+"?(.*?)"?\s+(BINARY|MOTOROLA|WAVE)\s*$', line, re.I)
            if m:
                files.append([os.path.join(base, m.group(1)), None])
                continue
            m = re.match(r"TRACK\s+\d+\s+(\S+)", line, re.I)
            if m and files and files[-1][1] is None:
                files[-1][1] = m.group(1).upper()
    return files


def describe(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".chd":
        return f"{path}: CHD not supported, convert it to CUE/BIN (chdman extractcd) or read the id from a BIN"
    if ext == ".cue":
        files = parse_cue(path)
        if not files:
            return f"{path}: no FILE in the CUE"
        data_file, mode = files[0]
        size = 2048 if mode == "MODE1/2048" else 2352
        data, size = sector0(data_file, size)
        total = 0
        for fpath, fmode in files:
            fsize = os.path.getsize(fpath)
            total += (fsize + (2048 if fmode == "MODE1/2048" else 2352) - 1) // (2048 if fmode == "MODE1/2048" else 2352)
    else:
        data, size = sector0(path)
        total = (os.path.getsize(path) + size - 1) // size
    if data[0] != 1 or data[1:6] != b"\x5a" * 5:
        return f"{path}: sector 0 is not an Opera label (not a 3DO data disc?)"
    vol_id = struct.unpack(">I", data[0x48:0x4C])[0]
    name = data[0x28:0x48].split(b"\x00")[0].decode("latin-1", "replace")
    lead = total + 150
    m, s, f = lead // 75 // 60, (lead // 75) % 60, lead % 75
    msf = (m << 24) | (s << 16) | (f << 8)
    return (f"{os.path.basename(path)}: volume \"{name}\"\n"
            f"  id  = 32'h{vol_id:08X}\n"
            f"  msf = 32'h{msf:08X}  (lead-out {m:02d}:{s:02d}:{f:02d}, {total} sectors)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for p in sys.argv[1:]:
        try:
            print(describe(p))
        except OSError as e:
            print(f"{p}: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
