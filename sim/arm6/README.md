# ARM6 core simulation

Standalone Verilator testbench for the ARM60 core in `rtl/ARM6`.

* `tb_arm6.sv` runs a program from address 0 with a zero-wait-state 64 KB RAM, big-endian like the 3DO.
  It drives the IRQ and FIQ lines: a program asserts them through `0xF0000010` / `0xF0000014`
  (0 releases the line, n > 0 asserts it n cycles later), and `+IRQRATE`, `+FIQRATE`, `+INTSEED`,
  `+INTMIN` assert them at random.
* `tests/*.S` are self-checking ARM programs (GNU as). They write `0x600D` to `0xF0000000` on success,
  or the failing test id, actual and expected values. A `@ maxcyc N` line raises the cycle limit.
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
| `t_swi` | SWI from SVC and User mode: R14_svc, SPSR_svc, mode and I bit, banked R13/R14; returns with MOVS PC, SUBS PC and LDM {..PC}^ |
| `t_irq` | IRQ and FIQ masking, entry state (mode, I/F, SPSR, R14), banked FIQ R8-R12, FIQ before IRQ; an IRQ, a FIQ or both at each of 200 cycles of a workload with every kind of instruction, which must give the same result as without interrupt |
| `repro/issue*` | the programs from issues #10, #11 and #12, unchanged |

## Status on the current main

On srg320's main (2cf0583) with the two RTL commits of this branch (immediate LSR #0, unaligned
LDR/SWP rotation), 8 of the 12 programs pass. The other 4 need canxin121's open pull requests:

| Program | Fails on | Fixed by |
|---|---|---|
| `t_msr` | `MSR SPSR_flg, Rm` also writes the control bits | MiSTer-devel/3DO_MiSTer#18 |
| `t_irq`, `t_swi` | their setup uses `MSR CPSR_flg, #imm`, decoded as an illegal instruction | MiSTer-devel/3DO_MiSTer#19 |
| `t_regress` (case 22) | LSL/LSR by a register holding 64 or more sets C | MiSTer-devel/3DO_MiSTer#17 |

`fuzz.py` also sets the flags with `MSR CPSR_flg, #imm`, so its programs need #19 too.

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

With `--irq`, the same programs check interrupts on the RTL alone, big-endian: each program
runs in SVC or User mode with IRQ and FIQ enabled, once without interrupt and `--runs` times
(default 6) with IRQs, FIQs or both at random times. The handlers save what they use (the FIQ
handler only uses its banked R8-R12), clobber the flags and return with `SUBS PC, R14, #4`, so
every run must end in the same state as the run without interrupt.

    python3 fuzz.py --irq -n 2000 -l 80 --runs 10 --out failures/
