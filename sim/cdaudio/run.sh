#!/usr/bin/env bash
# CD-ROM drive + XBUS slave testbench (block sizes, CD-DA, Sub-Q, abort). Needs verilator (5.x).
#   ./run.sh                   simulate rtl/
#   RTL=/path/to/rtl ./run.sh  simulate another copy of rtl/ (e.g. upstream)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="${RTL:-$HERE/../../rtl}"
BUILD="${BUILD:-$HOME/.cache/3do-cdaudio-sim}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac
rm -rf "$BUILD"
mkdir -p "$BUILD/src"
cp "$HERE/sim_models.sv" "$HERE/tb_cdaudio.sv" "$RTL/3DO_CDROM.sv" "$RTL/3DO/OSA.sv" "$BUILD/src/"
verilator --binary --timing -j 0 -Wno-fatal -Wno-lint -Wno-style --top-module tb_cdaudio --Mdir "$BUILD/obj_dir" \
	"$BUILD/src/sim_models.sv" "$BUILD/src/3DO_CDROM.sv" "$BUILD/src/OSA.sv" "$BUILD/src/tb_cdaudio.sv" \
	> "$BUILD/verilator.log" 2>&1 || { cat "$BUILD/verilator.log"; exit 1; }
"$BUILD/obj_dir/Vtb_cdaudio" | grep '^TB '
