#!/usr/bin/env python3
"""Rewrites, for Verilator 5.0, the variables that an always block declares with an initial value:

    always @(posedge clk) begin          initial begin sim_init_1.cnt = 5'h1f; end
        reg [4:0] cnt = 5'h1f;     ->    always @(posedge clk) begin : sim_init_1
        ...                                  reg [4:0] cnt;

Such a variable is static: its value is set once, before the first clock (IEEE 1800 6.21), which
is what Quartus builds. Verilator 5.0 runs the initial value as an assignment every time the
block runs, so the value is moved to an initial block that names the variable through the label
of the block. Usage: fix_inits.py FILE... (files are rewritten in place).
"""
import re
import sys

ALWAYS = re.compile(r"^(\s*always\w*\b.*?\bbegin\b)(\s*:\s*(\w+))?(.*)$")
DECL = re.compile(r"^(\s*)((?:reg|bit|logic|integer|int|byte|shortint|longint)\b"
                  r"(?:\s+signed)?\s*(?:\[[^\]]*\]\s*)*)(.*?);(\s*(?://.*)?)$")


def split_top(s):
    """Split on the commas that are not inside (), [] or {}."""
    out, depth, cur = [], 0, ""
    for c in s:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += c
    out.append(cur)
    return out


def fix(text, counter):
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        m = ALWAYS.match(lines[i])
        if not m or lines[i].lstrip().startswith("//"):
            out.append(lines[i])
            i += 1
            continue
        head, label, rest = m.group(1), m.group(3), m.group(4)
        j = i + 1
        decls, inits = [], []
        while j < len(lines):
            d = DECL.match(lines[j])
            if not d:
                if lines[j].strip() == "" or lines[j].strip().startswith("//"):
                    decls.append(lines[j])
                    j += 1
                    continue
                break
            parts = []
            for decl in split_top(d.group(3)):
                if "=" in decl:
                    name_part, value = decl.split("=", 1)
                    name = re.match(r"\s*(\w+)", name_part).group(1)
                    inits.append((name, value.strip()))
                    parts.append(name_part.rstrip())
                else:
                    parts.append(decl)
            decls.append(d.group(1) + d.group(2) + ",".join(parts) + ";" + d.group(4))
            j += 1
        if not inits:
            out.append(lines[i])
            i += 1
            continue
        if not label:
            counter[0] += 1
            label = "sim_init_%d" % counter[0]
            lines[i] = head + " : " + label + rest
        indent = re.match(r"\s*", lines[i]).group(0)
        out.append(indent + "initial begin " + " ".join("%s.%s = %s;" % (label, n, v) for n, v in inits) + " end")
        out.append(lines[i])
        out.extend(decls)
        i = j
    return "\n".join(out)


def main():
    counter = [0]
    for path in sys.argv[1:]:
        with open(path, encoding="latin-1") as f:
            text = f.read()
        new = fix(text, counter)
        if new != text:
            with open(path, "w", encoding="latin-1") as f:
                f.write(new)


if __name__ == "__main__":
    main()
