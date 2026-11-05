#!/usr/bin/env bash
# Builds the virtual MiSTer (Verilator) for the 3DO core of this repository, with the 3DO code of
# a Main_MiSTer clone, then runs it if arguments are given (see sim_main.cpp for the options).
#   MAIN_REPO=/path/to/Main_MiSTer [MAIN_REF=branch] ./run.sh [--bios FILE --cd FILE ...]
#   THREADS=N        Verilator threads (default 1)
#   BUILD=dir        build folder, without spaces (default ~/.cache/3do-system-sim)
#   CORE=dir         core to simulate (default: this repository)
#   VFLAGS="..."     extra Verilator options, e.g. +define+SIM to use srg320's simulation models
#                    of the memories (`ifdef SIM in rtl/) instead of the Intel blocks
# Needs verilator 5.x and g++.
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE="${CORE:-$HERE/../..}"
BUILD="${BUILD:-$HOME/.cache/3do-system-sim}"
THREADS="${THREADS:-1}"
: "${MAIN_REPO:?set MAIN_REPO to a Main_MiSTer clone}"
MAIN_REF="${MAIN_REF:-HEAD}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac

rm -rf "$BUILD/src" "$BUILD/main"
mkdir -p "$BUILD/src" "$BUILD/main"

# Core sources. Quartus skips the code between "synopsys translate_off" and "translate_on"
# (`define SIM and DEBUG, debug code); Verilator does not, so it is removed here: the code paths
# of the FPGA build are the ones simulated.
cp -r "$CORE/rtl" "$CORE/3DO.sv" "$BUILD/src/"
find "$BUILD/src" \( -name '*.sv' -o -name '*.v' \) -exec sed -i -e 's/\r$//' -e '/synopsys translate_off/,/synopsys translate_on/d' {} +
# Variables declared with an initial value inside an always block: see fix_inits.py
find "$BUILD/src" \( -name '*.sv' -o -name '*.v' \) -print0 | xargs -0 python3 -I "$HERE/fix_inits.py"
# Verilator 5.020 does not parse an unpacked array of nets declared with its value
# ("wire X[4] = '{...};"): written as a declaration and a continuous assignment.
find "$BUILD/src" -name '*.sv' -exec perl -pi -e "s/^(\s*wire\s+(?:\[[^\]]*\]\s*)?)(\w+)(\s*\[\d+\])\s*=\s*('\{)/\$1\$2\$3; assign \$2 = \$4/" {} +
# rtl/sdram.sv drives its data bus from a clocked block (SDRAM_DQ <= 'Z / <= d), a tristate form
# Verilator does not support: written as the same registered output enable and data.
perl -0pi -e "s/inout\s+reg\s+\[15: 0\] SDRAM_DQ,/inout      [15: 0] SDRAM_DQ,/;
              s/(\tlocalparam RASCAS_DELAY)/\treg [15:0] sim_dq; reg sim_dq_oe = 0;\n\tassign SDRAM_DQ = sim_dq_oe ? sim_dq : 16'hZZZZ;\n\$1/;
              s/SDRAM_DQ <= 'Z;/sim_dq_oe <= 0;/;
              s/SDRAM_DQ <= d;/begin sim_dq <= d; sim_dq_oe <= 1; end/;
              s/rbuf <= \{rbuf\[15:0\],SDRAM_DQ\};/rbuf <= {rbuf[15:0],sim_top.sdram_dq_chip_oe ? sim_top.sdram_dq_chip : 16'hFFFF};/" "$BUILD/src/rtl/sdram.sv"
[ "$(grep -c 'sim_dq' "$BUILD/src/rtl/sdram.sv")" -eq 4 ] && [ "$(grep -c 'sdram_dq_chip' "$BUILD/src/rtl/sdram.sv")" -eq 1 ] \
	|| { echo "sdram.sv data bus patch did not apply" >&2; exit 1; }
cp "$HERE"/sim_models.sv "$HERE"/hps_io_sim.sv "$HERE"/sim_top.sv "$HERE"/build_id.v \
   "$HERE"/sim_main.cpp "$HERE"/main_stubs.cpp "$BUILD/src/"

# 3DO code of Main_MiSTer, unchanged, with the rest of Main replaced by main_stubs
# (MAIN_DIR=folder takes it from a working folder instead of MAIN_REF)
if [ -n "${MAIN_DIR:-}" ]; then
	mkdir -p "$BUILD/main/support"
	cp -r "$MAIN_DIR/support/3do" "$BUILD/main/support/"
	cp "$MAIN_DIR/cd.h" "$BUILD/main/"
else
	git -c safe.directory='*' -C "$MAIN_REPO" archive "$MAIN_REF" support/3do cd.h | tar -x -C "$BUILD/main"
fi
cp -r "$HERE/main_stubs/." "$BUILD/main/"
# MAIN_SED="sed expression": experiment on the copy of 3docdd.cpp (the clone is not touched)
if [ -n "${MAIN_SED:-}" ]; then
	sed -i "$MAIN_SED" "$BUILD/main/support/3do/3docdd.cpp"
	echo "3docdd.cpp patched: $MAIN_SED"
fi

S="$BUILD/src"
RTL=( "$S/sim_models.sv" "$S/hps_io_sim.sv"
      "$S/rtl/3DO/3DO_pkg.sv" "$S/rtl/ARM6/ARM6_pkg.sv" "$S/rtl/ARM6/ARM6_RB.sv" "$S/rtl/ARM6/ARM6_CORE.sv" )
for f in "$S"/rtl/3DO/*.sv; do [ "$(basename "$f")" = 3DO_pkg.sv ] || RTL+=( "$f" ); done
for f in "$S"/rtl/*.sv; do RTL+=( "$f" ); done
RTL+=( "$S/rtl/hps_ext.v" "$S/3DO.sv" "$S/sim_top.sv" )

THREAD_OPT=()
[ "$THREADS" -gt 1 ] && THREAD_OPT=( --threads "$THREADS" )

cd "$BUILD"
verilator --cc --exe --build -O3 --x-assign 0 --x-initial 0 --no-timing \
	-Wno-fatal -Wno-lint -Wno-style -Wno-MULTIDRIVEN -Wno-PINMISSING -Wno-PINCONNECTEMPTY -Wno-PROCASSWIRE \
	--top-module sim_top -I"$S" --Mdir "$BUILD/obj_dir" -j "$(nproc)" "${THREAD_OPT[@]}" ${VFLAGS:-} \
	-CFLAGS "-O2 -I$BUILD/main -I$BUILD/main/include" \
	"${RTL[@]}" "$S/sim_main.cpp" "$S/main_stubs.cpp" \
	"$BUILD/main/support/3do/3do.cpp" "$BUILD/main/support/3do/3docdd.cpp" \
	> "$BUILD/verilator.log" 2>&1 || { grep -E "%Error|error:" "$BUILD/verilator.log" | head -n 40; echo "build failed, see $BUILD/verilator.log"; exit 1; }
echo "built: $BUILD/obj_dir/Vsim_top"

if [ $# -gt 0 ]; then
	"$BUILD/obj_dir/Vsim_top" "$@"
fi
