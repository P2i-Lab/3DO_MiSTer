#!/usr/bin/env python3
"""Differential testing of the ARM6 core (rtl/ARM6) against Unicorn (ARM926 model).

Random programs restricted to behaviours that the ARMv3 ARM60 and an ARMv5 core define
identically are assembled with GNU as (little-endian), run on the RTL (tb_arm6.sv built
with BIGEND=0) and on Unicorn, and their final state is compared: r0-r12, r14, CPSR,
SPSR and a 256-byte memory sandbox. A mismatch is reduced to a short program that ends
with the instruction that makes the results differ.

Not generated, because ARMv3 and ARMv5 (or Unicorn) differ or the result is unpredictable:
PC as an operand or destination, mode changes, unaligned word accesses (Unicorn does not
rotate them like pre-ARMv6 cores), MUL with Rd == Rm, writeback with Rd == Rn, LDM/STM
writeback with the base in the list, SWP with Rn == Rd or Rm, PSR bits 27-8.

With --irq, the same programs check interrupts instead (no Unicorn): each one runs in SVC
or User mode with IRQ and FIQ enabled, once without interrupt and RUNS times with IRQs
and FIQs at random times (tb_arm6.sv +IRQRATE/+FIQRATE/+INTSEED, big-endian like the
3DO). The handlers save what they use (FIQ: its banked R8-R12) and return with
SUBS PC,R14,#4, so every run must end in the same state.

Usage (Linux/WSL, needs verilator, binutils-arm-none-eabi and the unicorn Python module):
    fuzz.py [-n PROGRAMS] [-s SEED] [-l UNITS] [-j JOBS] [--rtl DIR] [--out DIR]
    fuzz.py --irq [--runs RUNS] [-n PROGRAMS] [-s SEED] [-l UNITS] [-j JOBS] [--rtl DIR] [--out DIR]
"""
import argparse
import hashlib
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REGS = [f"r{i}" for i in range(13)] + ["r14"]   # writable (r13 holds the dump pointer)
SRC_REGS = REGS + ["r13"]                        # readable
CONDS = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"]
DUMP, SANDBOX, SANDBOX_WORDS = 0x3000, 0x3100, 64
DUMP_WORDS = 16 + SANDBOX_WORDS
WORD_NAMES = REGS + ["cpsr", "spsr"] + [f"mem+{4 * i:#04x}" for i in range(SANDBOX_WORDS)]


# ---------------------------------------------------------------------------------------
# Program generation: a program is a list of units, each unit a list of assembly lines
# ---------------------------------------------------------------------------------------
def cond(rng):
    return "" if rng.random() < 0.5 else rng.choice(CONDS)


def rot_imm(rng):
    v, r = rng.randrange(256), rng.choice([0] * 4 + list(range(1, 16)))
    return ((v >> 2 * r) | (v << (32 - 2 * r))) & 0xFFFFFFFF if r else v


def operand2(rng):
    k = rng.randrange(10)
    if k < 3:
        return f"#{rot_imm(rng):#x}"
    rm = rng.choice(SRC_REGS)
    if k < 7:
        kind = rng.choice(["lsl", "lsr", "asr", "ror", "rrx", "reg"])
        if kind == "reg":
            return rm
        if kind == "rrx":
            return f"{rm}, rrx"
        amount = {"lsl": range(0, 32), "lsr": range(1, 33), "asr": range(1, 33), "ror": range(1, 32)}[kind]
        return f"{rm}, {kind} #{rng.choice(amount)}"
    return f"{rm}, {rng.choice(['lsl', 'lsr', 'asr', 'ror'])} {rng.choice(SRC_REGS)}"


def unit_dp(rng):
    op = rng.choice(["and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "orr", "bic", "mov", "mvn",
                     "tst", "teq", "cmp", "cmn"])
    c = cond(rng)
    if op in ("tst", "teq", "cmp", "cmn"):
        return [f"{op}{c} {rng.choice(SRC_REGS)}, {operand2(rng)}"]
    s = "s" if rng.random() < 0.5 else ""
    rd = rng.choice(REGS)
    if op in ("mov", "mvn"):
        return [f"{op}{s}{c} {rd}, {operand2(rng)}"]
    return [f"{op}{s}{c} {rd}, {rng.choice(SRC_REGS)}, {operand2(rng)}"]


def unit_mul(rng):
    rd = rng.choice(REGS)
    rm = rng.choice([r for r in REGS if r != rd])          # ARMv3: Rd must differ from Rm
    s, c = ("s" if rng.random() < 0.5 else ""), cond(rng)
    if rng.random() < 0.5:
        return [f"mul{s}{c} {rd}, {rm}, {rng.choice(SRC_REGS)}"]
    return [f"mla{s}{c} {rd}, {rm}, {rng.choice(SRC_REGS)}, {rng.choice(SRC_REGS)}"]


def unit_psr(rng):
    k = rng.randrange(5)
    if k == 0:
        return [f"mrs{cond(rng)} {rng.choice(REGS)}, cpsr"]
    if k == 1:
        return [f"mrs{cond(rng)} {rng.choice(REGS)}, spsr"]
    if k == 2:
        return [f"msr{cond(rng)} {rng.choice(['cpsr_f', 'spsr_f'])}, #{rng.randrange(16) << 28:#x}"]
    t = rng.choice(REGS)
    if k == 3:   # flags from a register (bits 27-24 cleared: Q/J exist on ARMv5 only)
        return [f"bic {t}, {rng.choice(SRC_REGS)}, #0x0f000000", f"msr{cond(rng)} {rng.choice(['cpsr_f', 'spsr_f'])}, {t}"]
    return [f"bic {t}, {rng.choice(SRC_REGS)}, #0x0ff00000", f"bic {t}, {t}, #0x000ff000",
            f"bic {t}, {t}, #0x00000f00", f"bic {t}, {t}, #0x00000020", f"msr{cond(rng)} spsr_fc, {t}"]


def sandbox_base(rng, base):
    off = 0x100 + 4 * rng.randrange(16, 48)                 # middle of the sandbox, word aligned
    return f"add {base}, r13, #{off:#x}"


def unit_ldst(rng):
    load, byte = rng.random() < 0.5, rng.random() < 0.4
    base = rng.choice(REGS)
    rt = rng.choice([r for r in REGS if r != base])
    op = ("ldr" if load else "str") + ("b" if byte else "") + cond(rng)
    lines = [sandbox_base(rng, base)]
    step = 1 if byte else 4
    sign = rng.choice(["", "-"])
    if rng.random() < 0.6:
        imm = step * rng.randrange(0, 60 // step + 1)
        mode = rng.randrange(3)
        if mode == 0:
            addr = f"[{base}, #{sign}{imm}]"
        elif mode == 1:
            addr = f"[{base}, #{sign}{imm}]!"
        else:
            addr = f"[{base}], #{sign}{imm}"
    else:
        idx = rng.choice([r for r in REGS if r not in (base, rt)])
        k = rng.randrange(0, 16)
        shift = rng.choice(["", ", lsl #2", ", lsr #32", ", asr #32", ", lsr #1"])
        if not byte and shift in ("", ", lsr #1"):
            k = 4 * rng.randrange(0, 4) * (2 if shift == ", lsr #1" else 1)
        lines.append(f"mov {idx}, #{k}")
        mode = rng.randrange(3)
        if mode == 0:
            addr = f"[{base}, {sign}{idx}{shift}]"
        elif mode == 1:
            addr = f"[{base}, {sign}{idx}{shift}]!"
        else:
            addr = f"[{base}], {sign}{idx}{shift}"
    return lines + [f"{op} {rt}, {addr}"]


def unit_ldm(rng):
    base = rng.choice(REGS)
    wb = rng.random() < 0.5
    pool = [r for r in REGS if not (wb and r == base)]
    regs = sorted(rng.sample(pool, rng.randrange(1, 9)), key=lambda r: int(r[1:]))
    op = rng.choice(["ldm", "stm"]) + rng.choice(["ia", "ib", "da", "db"]) + cond(rng)
    return [sandbox_base(rng, base), f"{op} {base}{'!' if wb else ''}, {{{', '.join(regs)}}}"]


def unit_swp(rng):
    base = rng.choice(REGS)
    rd = rng.choice([r for r in REGS if r != base])
    rm = rng.choice([r for r in REGS if r != base])
    return [sandbox_base(rng, base), f"swp{'b' if rng.random() < 0.4 else ''}{cond(rng)} {rd}, {rm}, [{base}]"]


def unit_branch(rng):
    body = [line for _ in range(rng.randrange(1, 3)) for line in unit_dp(rng)]
    return [f"{rng.choice(['b', 'bl'])}{rng.choice(CONDS + [''])} 1f"] + body + ["1:"]


UNIT_KINDS = [(unit_dp, 45), (unit_mul, 8), (unit_ldst, 22), (unit_ldm, 8), (unit_swp, 4), (unit_psr, 6),
              (unit_branch, 7)]


def gen_program(seed, length):
    rng = random.Random(seed)
    kinds, weights = zip(*UNIT_KINDS)
    units = [rng.choices(kinds, weights)[0](rng) for _ in range(length)]
    init = [rng.getrandbits(32) for _ in REGS]
    flags = rng.randrange(16) << 28
    spsr = (rng.randrange(16) << 28) | rng.choice([0x10, 0x13, 0x92, 0xD3, 0x11, 0x1B])
    sandbox = [rng.getrandbits(32) for _ in range(SANDBOX_WORDS)]
    return {"units": units, "init": init, "flags": flags, "spsr": spsr, "sandbox": sandbox}


IRQ_STACK, INT_COUNT = 0x8000, 0x7F00            # interrupt mode: IRQ stack, IRQ/FIQ counts
INT_MIN = 96        # cycles between an acknowledge and the next interrupt: more than the handlers take
INT_PROLOGUE = [                                  # vectors and handlers, see the module docstring
    "\tb int_reset", "\tb int_trap", "\tb int_trap", "\tb int_trap", "\tb int_trap", "\tb int_trap",
    "\tb int_irq", "\tb int_fiq",
    "int_trap:", "\tmov r12, #0xF0000000", "\tldr r0, =0xDEAD", "\tstr r0, [r12]", "1:\tb 1b",
    "int_irq:", "\tstmfd sp!, {r0, r1}", "\tmov r0, #0xF0000000", "\tmov r1, #0", "\tstr r1, [r0, #0x10]",
    f"\tldr r0, ={INT_COUNT:#x}", "\tldr r1, [r0]", "\tadd r1, r1, #1", "\tstr r1, [r0]",
    # User-bank transfers, as in a task switch: save User R13/R14, overwrite them, restore them
    "\tsub sp, sp, #8", "\tstmia sp, {r13, r14}^", "\tldr r0, =0xBAD0BAD0", "\tmov r1, r0", "\tstmfd sp!, {r0, r1}",
    "\tldmia sp, {r13, r14}^", "\tnop", "\tadd sp, sp, #8", "\tldmia sp, {r13, r14}^", "\tnop", "\tadd sp, sp, #8",
    "\tmsr cpsr_f, #0xF0000000", "\tldmfd sp!, {r0, r1}", "\tsubs pc, lr, #4",
    "int_fiq:", "\tmov r8, #0xF0000000", "\tmov r9, #0", "\tstr r9, [r8, #0x14]",
    f"\tldr r8, ={INT_COUNT + 4:#x}", "\tldr r9, [r8]", "\tadd r9, r9, #1", "\tstr r9, [r8]",
    "\tmvn r10, #0", "\tmov r11, r10", "\tmov r12, r10", "\tmsr cpsr_f, #0xF0000000", "\tsubs pc, lr, #4",
    "\t.ltorg",
    "int_reset:", "\tldr r0, =0xD2", "\tmsr cpsr_fc, r0", f"\tldr sp, ={IRQ_STACK:#x}"]


def program_source(p, units, irq=False):
    if irq:   # interrupt mode: the program runs in SVC (0x13) or User (0x10) mode, IRQ and FIQ enabled
        head = INT_PROLOGUE + [f"\tldr r0, ={0x10 if p['spsr'] & 1 else 0x13:#x}", "\tmsr cpsr_fc, r0"]
    else:
        head = []
    out = [".syntax unified", ".arm", ".text", ".global _start", "_start:"] + head + [
           f"\tldr r0, ={p['spsr']:#x}", "\tmsr spsr_fc, r0", f"\tmsr cpsr_f, #{p['flags']:#x}",
           "\tldr r0, =init_regs", "\tldmia r0, {r0-r12, r14}", f"\tldr r13, ={DUMP:#x}", "@ ---- body"]
    for u in units:
        out += [line if line.endswith(":") else "\t" + line for line in u]
    out += ["@ ---- dump", "\tstmia r13, {r0-r12, r14}", "\tmrs r0, cpsr", "\tstr r0, [r13, #56]",
            "\tmrs r0, spsr", "\tstr r0, [r13, #60]", "\tmov r12, #0xF0000000", "\tstr r0, [r12]",
            "9:\tb 9b", "\t.ltorg", "\t.align 2", "init_regs:"]
    out += [f"\t.word {v:#010x}" for v in p["init"]]
    out += [f"\t.org {DUMP:#x}", "\t.space 64"] + [f"\t.word {v:#010x}" for v in p["sandbox"]]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------------------
# Execution
# ---------------------------------------------------------------------------------------
def assemble(src, tmp, big=False):
    s, o, e, b = (os.path.join(tmp, n) for n in ("p.S", "p.o", "p.elf", "p.bin"))
    endian = "-EB" if big else "-EL"
    open(s, "w").write(src)
    r = subprocess.run(["arm-none-eabi-as", endian, "-march=armv4", "-o", o, s], capture_output=True, text=True)
    if r.returncode:
        msgs = [m.split(": ", 1)[-1] for m in r.stderr.splitlines() if "Error" in m]
        raise RuntimeError("assembler: " + (msgs[0] if msgs else r.stderr.strip()))
    subprocess.run(["arm-none-eabi-ld", endian, "-Ttext=0", "-e", "0", "-o", e, o], check=True, capture_output=True)
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", e, b], check=True)
    return open(b, "rb").read()


def run_rtl(sim, image, tmp, big=False, plusargs=()):
    words = struct.unpack(f"{'>' if big else '<'}{len(image) // 4}I", image[: len(image) // 4 * 4])
    memh = os.path.join(tmp, "p.memh")
    open(memh, "w").write("".join(f"{w:08x}\n" for w in words))
    r = subprocess.run([sim, f"+IMAGE={memh}", f"+DUMPADDR={DUMP:x}", f"+DUMPLEN={DUMP_WORDS}", "+MAXCYC=400000",
                        *plusargs], capture_output=True, text=True, cwd=tmp, timeout=60)
    dump, status, ints = {}, "no result", (0, 0)
    for line in r.stdout.splitlines():
        f = line.split()
        if line.startswith("TB DUMP"):
            dump[int(f[2])] = int(f[3], 16)
        elif line.startswith("TB DONE"):
            status = "ok" if "marker=0000dead" not in line else "trap vector reached"
        elif line.startswith("TB INTS"):
            ints = tuple(int(x.split("=")[1]) for x in f[2:4])
        elif line.startswith("TB ") and not line.startswith("TB MARKER"):
            status = line[3:]
    return status, [dump.get(i) for i in range(DUMP_WORDS)], ints


def run_unicorn(image):
    from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_LITTLE_ENDIAN, UC_HOOK_MEM_WRITE
    from unicorn import arm_const as A
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM | UC_MODE_LITTLE_ENDIAN)
    uc.ctl_set_cpu_model(A.UC_CPU_ARM_926)
    uc.mem_map(0, 0x10000)
    uc.mem_map(0xF0000000, 0x1000)
    uc.mem_write(0, image)
    uc.reg_write(A.UC_ARM_REG_CPSR, 0xD3)
    done = []

    def on_write(uc_, access, addr, size, value, data):
        if addr == 0xF0000000:
            done.append(1)
            uc_.emu_stop()
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    try:
        uc.emu_start(0, 0x10000, count=20000)
    except UcError as ex:
        return f"unicorn error {ex}", [None] * DUMP_WORDS
    if not done:
        return "unicorn: no result", [None] * DUMP_WORDS
    return "ok", list(struct.unpack(f"<{DUMP_WORDS}I", uc.mem_read(DUMP, 4 * DUMP_WORDS)))


def compare(sim, p, units):
    tmp = tempfile.mkdtemp(prefix="fz")
    try:
        image = assemble(program_source(p, units), tmp)
        rs, rw, _ = run_rtl(sim, image, tmp)
        us, uw = run_unicorn(image)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    diffs = [(WORD_NAMES[i], rw[i], uw[i]) for i in range(DUMP_WORDS) if rw[i] != uw[i]]
    return (rs, us, diffs) if (rs != us or diffs) else None


def int_runs(seed, count):
    """Interrupt settings of the --irq runs of a program: (IRQ rate, FIQ rate, seed)."""
    rng = random.Random(seed * 7919 + 17)
    runs = []
    for k in range(count):
        kind = k % 3                            # IRQ only, FIQ only, both
        irq = rng.randrange(3, 41) if kind != 1 else 0
        fiq = rng.randrange(3, 41) if kind != 0 else 0
        runs.append((irq, fiq, rng.randrange(1, 1 << 31)))
    return runs


def compare_irq(sim, p, units, runs, taken=None):
    """None if every run with interrupts ends like the run without; else the first difference."""
    tmp = tempfile.mkdtemp(prefix="fzi")
    try:
        image = assemble(program_source(p, units, irq=True), tmp, big=True)
        rs, rw, _ = run_rtl(sim, image, tmp, big=True)
        if rs != "ok":
            raise RuntimeError(f"run without interrupt: {rs}")
        for irq, fiq, s in runs:
            st, w, ints = run_rtl(sim, image, tmp, big=True, plusargs=(f"+IRQRATE={irq}", f"+FIQRATE={fiq}", f"+INTSEED={s}",
                                                                       f"+INTMIN={INT_MIN}"))
            if taken is not None:
                taken[0] += ints[0]
                taken[1] += ints[1]
            diffs = [(WORD_NAMES[i], w[i], rw[i]) for i in range(DUMP_WORDS) if w[i] != rw[i]]
            if st != "ok" or diffs:
                return (f"IRQRATE={irq} FIQRATE={fiq} INTSEED={s} irq={ints[0]} fiq={ints[1]}", st, diffs)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return None


def reduce(cmpf, units):
    lo, hi = 1, len(units)                     # smallest failing prefix (results assumed to stay different)
    while lo < hi:
        mid = (lo + hi) // 2
        if cmpf(units[:mid]):
            hi = mid
        else:
            lo = mid + 1
    keep = units[:lo]
    i = 0
    while i < len(keep) - 1:                   # drop earlier units that are not needed
        trial = keep[:i] + keep[i + 1:]
        if cmpf(trial):
            keep = trial
        else:
            i += 1
    return keep, cmpf(keep)


def check_one(args):
    sim, seed, length, irq_runs = args
    p = gen_program(seed, length)
    taken = [0, 0]
    try:
        if irq_runs:
            runs = int_runs(seed, irq_runs)
            bad = compare_irq(sim, p, p["units"], runs, taken)
        else:
            bad = compare(sim, p, p["units"])
    except Exception as ex:                    # generator/assembler problem, not a CPU mismatch
        return seed, "error", str(ex), taken
    if not bad:
        return seed, "pass", None, taken
    try:
        if irq_runs:                           # keep the interrupt settings of the failing run
            failing = [r for r in runs if bad[0].startswith(f"IRQRATE={r[0]} FIQRATE={r[1]} INTSEED={r[2]} ")]
            keep, result = reduce(lambda u: compare_irq(sim, p, u, failing), p["units"])
        else:
            keep, result = reduce(lambda u: compare(sim, p, u), p["units"])
    except Exception:
        keep, result = p["units"], None
    if result is None:                         # the failure needs the whole program: report it as is
        keep, result = p["units"], bad
    return seed, "fail", (keep, result, p), taken


# ---------------------------------------------------------------------------------------
def build_sim(rtl, build, big=False):
    srcs = [os.path.join(rtl, f) for f in ("ARM6_pkg.sv", "ARM6_RB.sv", "ARM6_CORE.sv")] + [os.path.join(HERE, "tb_arm6.sv")]
    digest = hashlib.md5(b"".join(open(f, "rb").read() for f in srcs) + bytes([big])).hexdigest()
    sim = os.path.join(build, "obj_dir", "Vtb_arm6")
    stamp = os.path.join(build, "obj_dir", ".srcsum")
    if os.path.exists(sim) and os.path.exists(stamp) and open(stamp).read() == digest:
        return sim
    shutil.rmtree(build, ignore_errors=True)
    os.makedirs(os.path.join(build, "src"))
    for f in srcs:
        shutil.copy(f, os.path.join(build, "src"))
    print(f"== verilator (BIGEND={int(big)}): {rtl}", flush=True)
    r = subprocess.run(["verilator", "--binary", "--timing", "-j", "0", "-Wno-fatal", "-Wno-lint", "-Wno-style",
                        "-DDEBUG", f"-GBIGEND={int(big)}", "--top-module", "tb_arm6", "--Mdir", os.path.join(build, "obj_dir")]
                       + [os.path.join(build, "src", os.path.basename(f)) for f in srcs], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    open(stamp, "w").write(digest)
    return sim


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", type=int, default=500, help="number of programs")
    ap.add_argument("-s", type=int, default=1, help="first seed")
    ap.add_argument("-l", type=int, default=40, help="units per program")
    ap.add_argument("-j", type=int, default=os.cpu_count(), help="parallel jobs")
    ap.add_argument("--irq", action="store_true", help="check interrupts (runs with and without, no Unicorn)")
    ap.add_argument("--runs", type=int, default=6, help="--irq: runs with interrupts per program")
    ap.add_argument("--rtl", default=os.path.join(HERE, "..", "..", "rtl", "ARM6"))
    ap.add_argument("--build", default=None, help="Verilator build directory (default in ~/.cache)")
    ap.add_argument("--out", default=None, help="directory for the reduced failing programs")
    a = ap.parse_args()
    build = a.build or os.path.expanduser("~/.cache/3do-arm6-fuzz" + ("-irq" if a.irq else ""))
    sim = build_sim(os.path.abspath(a.rtl), build, big=a.irq)
    runs = a.runs if a.irq else 0
    counts, culprits, errors, shown, taken = Counter(), Counter(), Counter(), 0, [0, 0]
    with ProcessPoolExecutor(a.j) as pool:
        for seed, status, info, ints in pool.map(check_one, [(sim, a.s + i, a.l, runs) for i in range(a.n)]):
            counts[status] += 1
            taken[0] += ints[0]
            taken[1] += ints[1]
            if status == "error":
                errors[info.splitlines()[0][:120]] += 1
            elif status == "fail":
                keep, result, p = info
                last = keep[-1][-1] if not keep[-1][-1].endswith(":") else keep[-1][0]
                key = last.split()[0]
                culprits[key] += 1
                if shown < 12:
                    shown += 1
                    print(f"\n### seed {seed}: mismatch ({len(keep)} units after reduction)")
                    for u in keep:
                        print("   " + " ; ".join(u))
                    hex8 = lambda v: "None" if v is None else f"{v:08x}"
                    if a.irq:
                        params, st, diffs = result
                        print(f"   {params}: {st}")
                        for name, wv, rv in diffs[:8]:
                            print(f"   {name:9} with interrupts={hex8(wv)}  without={hex8(rv)}")
                    else:
                        rs, us, diffs = result
                        if rs != us:
                            print(f"   status: RTL={rs} Unicorn={us}")
                        for name, rv, uv in diffs[:8]:
                            print(f"   {name:9} RTL={hex8(rv)}  Unicorn={hex8(uv)}")
                if a.out:
                    os.makedirs(a.out, exist_ok=True)
                    open(os.path.join(a.out, f"seed{seed}.S"), "w").write(program_source(p, keep, irq=a.irq))
    print(f"\n== {a.n} programs: {counts['pass']} pass, {counts['fail']} mismatch, {counts['error']} generator errors")
    if a.irq:
        print(f"   interrupts taken: {taken[0]} IRQ, {taken[1]} FIQ")
    for k, v in culprits.most_common():
        print(f"   mismatch ending with '{k}': {v}")
    for k, v in errors.most_common(5):
        print(f"   error: {v} x {k}")
    sys.exit(1 if counts["fail"] else 0)


if __name__ == "__main__":
    main()
