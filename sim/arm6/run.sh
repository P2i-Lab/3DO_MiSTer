#!/usr/bin/env bash
# ARM6 core simulation (Verilator): builds the testbench, then runs the self-checking
# programs in tests/ and the reproduction images from bug reports in repro/.
#
#   ./run.sh                        run everything
#   ./run.sh msr issue10            run the tests whose name contains one of the words
#   RTL=/path/to/ARM6 ./run.sh      simulate another copy of rtl/ARM6 (e.g. upstream)
#   TRACE=1 ./run.sh t_msr          also write <build>/trace.fst (open it with GTKWave)
#
# Needs verilator (5.x) and binutils-arm-none-eabi. BUILD must be a path without
# spaces (Verilator generates Makefiles); by default it lives in ~/.cache.
set -euo pipefail
shopt -s nullglob

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RTL="${RTL:-$HERE/../../rtl/ARM6}"
BUILD="${BUILD:-$HOME/.cache/3do-arm6-sim}"
TRACE="${TRACE:-0}"
case "$BUILD" in *" "*) echo "BUILD must not contain spaces: $BUILD" >&2; exit 2;; esac
mkdir -p "$BUILD/src"

# ---- simulator -----------------------------------------------------------------------
SRCS=("$RTL/ARM6_pkg.sv" "$RTL/ARM6_RB.sv" "$RTL/ARM6_CORE.sv" "$HERE/tb_arm6.sv")
VFLAGS=(--binary --timing -j 0 -Wno-fatal -Wno-lint -Wno-style -DDEBUG --top-module tb_arm6)
MDIR="$BUILD/obj_dir"
if [[ "$TRACE" == 1 ]]; then VFLAGS+=(--trace-fst -DTRACE); MDIR="$BUILD/obj_dir_trace"; fi
SIM="$MDIR/Vtb_arm6"

sum="$(cat "${SRCS[@]}" | md5sum | cut -d' ' -f1)"
if [[ ! -x "$SIM" || "$(cat "$MDIR/.srcsum" 2>/dev/null)" != "$sum" ]]; then
	echo "== verilator: $RTL"
	rm -rf "$MDIR"
	cp "${SRCS[@]}" "$BUILD/src/"
	if ! verilator "${VFLAGS[@]}" --Mdir "$MDIR" \
		"$BUILD/src/ARM6_pkg.sv" "$BUILD/src/ARM6_RB.sv" "$BUILD/src/ARM6_CORE.sv" "$BUILD/src/tb_arm6.sv" \
		> "$BUILD/verilator.log" 2>&1; then
		cat "$BUILD/verilator.log"
		exit 1
	fi
	echo "$sum" > "$MDIR/.srcsum"
fi

# ---- helpers -------------------------------------------------------------------------
assemble() {	# tests/<name>.S -> $BUILD/<name>.memh (big-endian words, one per line)
	local n="$1"
	arm-none-eabi-as -EB -march=armv4 -I "$HERE/tests" -o "$BUILD/$n.o" "$HERE/tests/$n.S"
	arm-none-eabi-ld -EB -Ttext=0 -e 0 -o "$BUILD/$n.elf" "$BUILD/$n.o"
	arm-none-eabi-objcopy -O binary "$BUILD/$n.elf" "$BUILD/$n.bin"
	od -An -v -tx1 -w4 "$BUILD/$n.bin" | tr -d ' ' > "$BUILD/$n.memh"
}
simulate() {	# simulate <name> <memh> [plusargs...]: output in $BUILD/<name>.log
	local n="$1" img="$2"
	shift 2
	(cd "$BUILD" && "$SIM" +IMAGE="$img" "$@") > "$BUILD/$n.log" 2>&1 || true
}
selected() {	# true if no filter word was given or <name> contains one of them
	(( ${#FILTERS[@]} == 0 )) && return 0
	local w
	for w in "${FILTERS[@]}"; do [[ "$1" == *"$w"* ]] && return 0; done
	return 1
}
report() {	# report <name> <ok> [detail]
	if [[ "$2" == 1 ]]; then echo "PASS  $1"; pass=$((pass + 1))
	else echo "FAIL  $1  ${3:-}"; fail=$((fail + 1)); fi
}

# ---- run -----------------------------------------------------------------------------
FILTERS=("$@")
pass=0
fail=0
for src in "$HERE"/tests/t_*.S; do
	n="$(basename "$src" .S)"
	selected "$n" || continue
	assemble "$n"
	maxcyc="$(sed -n 's/^@ maxcyc \([0-9][0-9]*\).*/\1/p' "$src" | head -n 1)"	# "@ maxcyc N" in the test
	simulate "$n" "$BUILD/$n.memh" ${maxcyc:++MAXCYC=$maxcyc}
	if grep -q '^TB DONE marker=0000600d' "$BUILD/$n.log"; then
		report "$n" 1
	else
		report "$n" 0 "$(grep '^TB ' "$BUILD/$n.log" | grep -v '^TB INTS' | head -n 1 || tail -n 3 "$BUILD/$n.log")"
	fi
done
for img in "$HERE"/repro/*.memh; do
	n="$(basename "$img" .memh)"
	selected "$n" || continue
	cp "$img" "$BUILD/$n.memh"
	simulate "$n" "$BUILD/$n.memh"
	missing="$(grep -vxF -f "$BUILD/$n.log" "$HERE/repro/$n.expect" || true)"
	if [[ -z "$missing" ]]; then
		report "$n" 1
	else
		report "$n" 0 "missing: $(head -n 2 <<< "$missing" | paste -sd ';')  got: $(grep -m1 '^TB ' "$BUILD/$n.log" || echo none)"
	fi
done
echo "== $pass passed, $fail failed"
(( fail == 0 ))
