#!/usr/bin/env bash
# Pause meter testbench (rtl/pause_meter.sv). Needs verilator (5.x); python3 to turn the
# picture into meter.png.
#   ./run.sh                     simulate rtl/
#   RTL=/path/to/rtl ./run.sh    simulate another copy of rtl/
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="${RTL:-$HERE/../../rtl}"
BUILD="${BUILD:-$HOME/.cache/3do-pause-meter-sim}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac
rm -rf "$BUILD"
mkdir -p "$BUILD/src"
cp "$RTL/pause_meter.sv" "$HERE/tb_pause_meter.sv" "$BUILD/src/"
verilator --binary --timing --timescale 1ns/1ps -j 0 -Wno-fatal -Wno-lint -Wno-style --top-module tb_pause_meter \
	--Mdir "$BUILD/obj_dir" "$BUILD/src/pause_meter.sv" "$BUILD/src/tb_pause_meter.sv" \
	> "$BUILD/verilator.log" 2>&1 || { cat "$BUILD/verilator.log"; exit 1; }
OUT="$(cd "$BUILD" && ./obj_dir/Vtb_pause_meter)"
grep '^TB ' <<< "$OUT"
if command -v python3 > /dev/null && [ -f "$BUILD/meter.ppm" ]; then
	python3 "$HERE/ppm2png.py" "$BUILD/meter.ppm" "$BUILD/meter.png" 4 && echo "picture: $BUILD/meter.png"
fi
grep -q '^TB PASS' <<< "$OUT"
