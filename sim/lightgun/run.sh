#!/usr/bin/env bash
# Light gun testbench (rtl/lightgun.sv). Needs verilator (5.x); python3 to turn the crosshair
# picture into cross.png.
#   ./run.sh                     simulate rtl/
#   RTL=/path/to/rtl ./run.sh    simulate another copy of rtl/
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="${RTL:-$HERE/../../rtl}"
BUILD="${BUILD:-$HOME/.cache/3do-lightgun-sim}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac
rm -rf "$BUILD"
mkdir -p "$BUILD/src"
cp "$RTL/lightgun.sv" "$HERE/tb_lightgun.sv" "$BUILD/src/"
verilator --binary --timing --timescale 1ns/1ps -j 0 -Wno-fatal -Wno-lint -Wno-style --top-module tb_lightgun \
	--Mdir "$BUILD/obj_dir" "$BUILD/src/lightgun.sv" "$BUILD/src/tb_lightgun.sv" \
	> "$BUILD/verilator.log" 2>&1 || { cat "$BUILD/verilator.log"; exit 1; }
OUT="$(cd "$BUILD" && ./obj_dir/Vtb_lightgun)"
grep '^TB ' <<< "$OUT"
if command -v python3 > /dev/null && [ -f "$BUILD/cross.ppm" ]; then
	python3 "$HERE/../pause_meter/ppm2png.py" "$BUILD/cross.ppm" "$BUILD/cross.png" 2 && echo "picture: $BUILD/cross.png"
fi
grep -q '^TB PASS' <<< "$OUT"
