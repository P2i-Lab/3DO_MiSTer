#!/usr/bin/env bash
# Host test of Main_MiSTer support/3do/3docdd.cpp (CD-DA, raw blocks, subcode, TOC). Needs g++.
#   MAIN=/path/to/Main_MiSTer ./run.sh
# Builds 3docdd.cpp with stubbed file/CHD access (stubs/), writes CUE/BIN/ISO images to a scratch
# folder and checks every byte Main_MiSTer would send to the core. Prints MAIN TEST PASS or FAIL.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${MAIN:?set MAIN to a Main_MiSTer checkout}"
BUILD="${BUILD:-$HOME/.cache/3do-main-cdtest}"
rm -rf "$BUILD"
mkdir -p "$BUILD/support/3do" "$BUILD/run"
cp -r "$HERE/stubs/." "$BUILD/"
cp "$HERE/test_3docdd.cpp" "$BUILD/"
cp "$MAIN/cd.h" "$BUILD/"
cp "$MAIN/support/3do/3do.h" "$MAIN/support/3do/3docdd.cpp" "$BUILD/support/3do/"
cd "$BUILD"
g++ -std=gnu++17 -O1 -Wall -Wno-unused-result -Wno-format-truncation -I. -Iinclude \
	test_3docdd.cpp support/3do/3docdd.cpp -o run/test
cd run && ./test
