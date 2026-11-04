#!/usr/bin/env bash
# NVRAM save file testbench (loading and saving through the logic of 3DO.sv). Needs verilator
# (5.x) and python3.
#   ./run.sh                          simulate this core (3DO.sv and rtl/ two levels up)
#   CORE=/path/to/core ./run.sh       simulate another copy of the core (e.g. upstream)
#   MAIN=/path/to/Main_MiSTer ./run.sh  also check the blank save against Main_MiSTer's
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE="${CORE:-$HERE/../..}"
BUILD="${BUILD:-$HOME/.cache/3do-nvram-sim}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac
rm -rf "$BUILD"
mkdir -p "$BUILD/src"
python3 "$HERE/prepare.py" "$CORE" "$BUILD"
cp "$HERE/sim_models.sv" "$HERE/tb_nvram.sv" "$BUILD/src/"
SRCS=("$BUILD/src/sim_models.sv")
if [ -f "$CORE/rtl/nvram_fmt.sv" ]; then
	cp "$CORE/rtl/nvram_fmt.sv" "$BUILD/src/"
	SRCS+=("$BUILD/src/nvram_fmt.sv")
fi
SRCS+=("$BUILD/src/tb_nvram.sv")
verilator --binary --timing --timescale 1ns/1ps -j 0 -Wno-fatal -Wno-lint -Wno-style --top-module tb_nvram -I"$BUILD" --Mdir "$BUILD/obj_dir" \
	"${SRCS[@]}" > "$BUILD/verilator.log" 2>&1 || { cat "$BUILD/verilator.log"; exit 1; }
OUT="$("$BUILD/obj_dir/Vtb_nvram" +IMG="$BUILD/img")"
grep '^TB ' <<< "$OUT"
grep -q '^TB PASS' <<< "$OUT"
