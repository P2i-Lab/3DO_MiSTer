# ARM6 core simulation

Standalone Verilator testbench for the ARM60 core in `rtl/ARM6`.

* `tb_arm6.sv` runs a program from address 0 with a zero-wait-state 64 KB RAM, big-endian like the 3DO.
* `tests/*.S` are self-checking ARM programs (GNU as). They write `0x600D` to `0xF0000000` on success,
  or the failing test id, actual and expected values.
* `repro/*.memh` are the exact programs from bug reports, with the expected results in `repro/*.expect`.

## Requirements

Linux or WSL2 with Verilator 5 and the ARM bare-metal binutils, e.g. on Ubuntu 24.04:

    sudo apt install verilator binutils-arm-none-eabi

## Usage

    ./run.sh                     # build and run everything
    ./run.sh msr issue10         # only the programs whose name contains "msr" or "issue10"
    TRACE=1 ./run.sh t_msr       # also dump ~/.cache/3do-arm6-sim/trace.fst (GTKWave)
    RTL=/path/to/ARM6 ./run.sh   # simulate another copy of rtl/ARM6

| Program | Covers |
|---|---|
| `t_shift_carry` | a zero shift amount keeps C (#12): unrotated immediate, LSL #0, shift by a register holding 0 |
| `t_shift_imm32` | immediate LSR #0 / ASR #0 mean #32 (#11), RRX, scaled register offsets |
| `t_msr` | MSR field masks (#10), SPSR_flg, MSR immediate, banked SPSRs, User mode, SWI |
| `t_unaligned` | unaligned LDR/SWP rotate the word by 8 × address[1:0] (big-endian halfword idiom), LDM and LDRB do not |
| `t_regress` | shifter, carry and overflow, MUL/MLA, big-endian byte lanes, LDM/STM, SWP, BL, conditions, PC reads |
| `repro/issue*` | the programs from issues #10, #11 and #12, unchanged |

## Differential fuzzing

`fuzz.py` runs random programs on the RTL and on [Unicorn](https://www.unicorn-engine.org/)
(ARM926 model) and compares r0-r12, r14, CPSR, SPSR and a 256-byte memory sandbox. Programs
mix data processing (every shift form, conditions, flags), MUL/MLA, LDR/STR(B) in every
addressing mode, LDM/STM, SWP, MRS/MSR and conditional branches, restricted to what ARMv3 and
ARMv5 define the same way (no PC operands, no mode changes, aligned word accesses, no
unpredictable register combinations, no PSR bits 27-8). A mismatch is reduced to a short
program ending with the offending instruction. It runs little-endian (`-GBIGEND=0`), since
Unicorn cannot run big-endian ARMv5 code; the directed tests cover the big-endian byte lanes.

    python3 -m venv ~/fuzzenv && ~/fuzzenv/bin/pip install unicorn
    ~/fuzzenv/bin/python fuzz.py -n 2000 -s 1 --out failures/   # programs, first seed
    ~/fuzzenv/bin/python fuzz.py --rtl /path/to/ARM6 ...        # another copy of rtl/ARM6
