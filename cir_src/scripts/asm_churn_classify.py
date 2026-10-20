#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Classify the assembly differences between two compilations of the same
sources (two LLVM pins, or two library versions), per file and per function.

The question it answers for an LLVM bump (M5p): how much of the churn is the
by-value record rewrite of upstream 036efe53 (records are copied through
memory at every coercion site) and nothing else.  For every function the two
assemblies are compared at increasing levels of normalization:

  identical      the same text
  frame          the same after stack offsets, the frame size and the CFI
                 offsets are normalized (frame layout only)
  copies         the same after dropping every data-movement instruction
                 (mov*, lea*, push, pop, cltq/cqto/cwtl-style extensions),
                 register zeroing (xor r, r) and calls to memcpy, memmove
                 and memset: copies through memory, nothing else
  calls          the same calls in the same order, but the instruction
                 stream differs after the steps above (arithmetic, compares
                 or jumps differ): look at it
  other          the call sequence or the control flow (jumps and labels)
                 differs: look at it

The LLVM IR (--emit=llvm) of the two compilations is classified in the
stream mode (`ir-` classes), to tell a change of the library, of the lowering
or of the attributes from a change of the LLVM code generator.  The IR is
compared up to the numbering of values, metadata and attribute groups only:
every attribute group is resolved to its attributes at the function or call
that uses it, and the module level (declarations, globals, module flags and
other metadata) is compared too, so a changed attribute (nounwind, noundef,
dso_local, local_unnamed_addr, ...) or declaration is a difference.  The
numbering of unnamed basic blocks is ignored.  IR classes, split by where the
difference is (body or module level) and by what it is: `ir-identical`;
`ir-attrs` (only attributes differ, on calls inside bodies or on functions,
parameters, returns and declarations at module level: the `nounwind` and
`noundef` kind of change); `ir-copies` (function bodies the same after
dropping alloca, load, store, getelementptr, bitcast, lifetime and
memcpy/memmove/memset instructions: the record copies; attributes aside);
`ir-module` (bodies equal, module level different beyond attributes: a
declaration, a global, a flag); `ir-other` (an instruction of a body
differs, or the set of functions).  The worst class of a file wins (other,
module, copies, attrs).  A file is
`backend-only` only when its IR is `ir-identical` and its assembly differs:
upstream LLVM code generation, nothing the library or the lowering did.

A file is classified by its worst function: identical < frame < copies <
calls < other.  A file whose set of functions differs, or whose data (the
text outside the functions) differs beyond local label numbers, is
`structure`.  A call to memcpy/memmove/memset added or removed is reported
as a note, whatever the class.

usage:
  asm_churn_classify.py OLD_DIR NEW_DIR        two directories of .s files with
                                               the same names (asm-diff output)
  asm_churn_classify.py --streams DIR OLD_EDG_CIR NEW_EDG_CIR [--opt=-O0] [-j N]
                                               compile every .ecil of DIR with
                                               both tools first
  --self-test                                  check the IR classes
  -v prints the files of every class but `identical`.
In directory mode a file that exists only in NEW is reported as
`only-new` (a file only in OLD is `status`).  Prints the counts per class (and
per note) and exits 0 (2 when nothing was compared).
"""
import argparse
import collections
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

CLASSES = ["identical", "frame", "copies", "calls", "other", "structure",
           "backend-only", "status", "only-new"]

FRAME_RES = [
    (re.compile(r"-?\d+\((%r[sb]p)\)"), r"OFF(\1)"),
    (re.compile(r"\$\d+, %rsp"), "$N, %rsp"),
    (re.compile(r"\.cfi_def_cfa_offset \d+"), ".cfi_def_cfa_offset N"),
    (re.compile(r"\.cfi_offset (%\w+), -?\d+"), r".cfi_offset \1, N"),
]
LABEL_RE = re.compile(r"\.L(BB|func_end|tmp|CPI|set|cfi|eh_func_end|exception)"
                      r"\d+(_\d+)?")
MOVE_RE = re.compile(r"^\s*(mov\w*|lea\w*|push\w*|pop\w*|cltq|cqto|cwtl|cltd"
                     r"|xchg\w*)\b")
CALL_RE = re.compile(r"^\s*(call|callq)\s+(\S+)")
MEM_RE = re.compile(r"^(memcpy|memmove|memset)(@PLT)?$")
JUMP_RE = re.compile(r"^\s*(j\w+|ret\w*|ud2|hlt)\b")


def split(text):
    """(functions: name -> list of lines, rest: list of lines)"""
    funcs = {}
    rest = []
    cur = None
    name = None
    for line in text.splitlines():
        if cur is None:
            m = re.match(r"^([A-Za-z_$][\w.$@]*):\s*(#.*)?$", line)
            if m and rest and rest[-1].startswith("\t.type\t") and \
                    "@function" in rest[-1]:
                name = m.group(1)
                cur = []
                continue
            rest.append(line)
        else:
            if line.startswith("\t.size\t" + name + ","):
                funcs[name] = cur
                cur = None
            else:
                cur.append(line)
    if cur is not None:
        funcs[name] = cur
    return funcs, rest


def norm_labels(line):
    return LABEL_RE.sub(lambda m: ".L" + m.group(1) + "N", line)


def norm_frame(line):
    for rx, rep in FRAME_RES:
        line = rx.sub(rep, line)
    return norm_labels(line)


def strip_comment(line):
    return line.split("#", 1)[0].rstrip()


def keep_lines(lines):
    return [strip_comment(l) for l in lines if strip_comment(l).strip()]


def calls_of(lines):
    out = []
    for l in lines:
        m = CALL_RE.match(l)
        if m:
            out.append(m.group(2))
    return out


def control_of(lines):
    out = []
    for l in lines:
        if JUMP_RE.match(l) or re.match(r"^\.L\w+:", l):
            out.append(norm_labels(l.strip()))
    return out


ZERO_RE = re.compile(r"^\s*xor[lq]\s+(%\w+), \1\s*$")
MEMCALL_RE = re.compile(r"^\s*callq?\s+(memcpy|memmove|memset)(@PLT)?\s*$")


def is_copy_line(line):
    return bool(MOVE_RE.match(line) or ZERO_RE.match(line) or
                MEMCALL_RE.match(line))


def classify_function(old, new):
    a = keep_lines(old)
    b = keep_lines(new)
    if a == b:
        return "identical", set()
    notes = set()
    ca, cb = calls_of(a), calls_of(b)
    for fn in ("memcpy", "memmove", "memset"):
        if sum(1 for c in ca if MEM_RE.match(c) and c.startswith(fn)) != \
                sum(1 for c in cb if MEM_RE.match(c) and c.startswith(fn)):
            notes.add(fn + " count changed")
    na = [norm_frame(l) for l in a]
    nb = [norm_frame(l) for l in b]
    if na == nb:
        return "frame", notes
    da = [l for l in na if not is_copy_line(l)]
    db = [l for l in nb if not is_copy_line(l)]
    if da == db:
        return "copies", notes
    ca = [c for c in ca if not MEM_RE.match(c)]
    cb = [c for c in cb if not MEM_RE.match(c)]
    if ca == cb and control_of(a) == control_of(b):
        return "calls", notes
    return "other", notes


def classify_file(old_text, new_text):
    fa, ra = split(old_text)
    fb, rb = split(new_text)
    notes = set()
    if set(fa) != set(fb):
        return "structure", {"functions only in old: %d, only in new: %d" %
                             (len(set(fa) - set(fb)), len(set(fb) - set(fa)))}
    worst = "identical"
    for name in fa:
        c, n = classify_function(fa[name], fb[name])
        notes |= n
        if CLASSES.index(c) > CLASSES.index(worst):
            worst = c
    ra = [norm_labels(strip_comment(l)) for l in ra if strip_comment(l).strip()]
    rb = [norm_labels(strip_comment(l)) for l in rb if strip_comment(l).strip()]
    if sorted(ra) != sorted(rb):
        return "structure", notes | {"data or directives outside the "
                                     "functions differ"}
    return worst, notes


IR_MEM_RE = re.compile(r"^\s*(%[\w.]+ = )?(alloca|load|store|getelementptr|"
                       r"bitcast|call void @llvm\.(memcpy|memmove|memset|"
                       r"lifetime)|tail call void @llvm\.(memcpy|memmove|"
                       r"memset))\b")
ATTR_GROUP_RE = re.compile(r"^attributes #(\d+) = \{(.*)\}\s*$")


def attr_groups(text):
    groups = {}
    for line in text.splitlines():
        m = ATTR_GROUP_RE.match(line)
        if m:
            groups[m.group(1)] = " ".join(sorted(m.group(2).split()))
    return groups


def resolve_attrs(line, groups):
    """Replace every #N attribute-group reference by ATTR(the attributes)."""
    return re.sub(r"#(\d+)\b", lambda m: "ATTR(" + groups.get(m.group(1), "?")
                  + ")", line)


ATTR_TEXT_RE = re.compile(r"ATTR\([^()]*\)")


def norm_ir(line, groups):
    """Up to value, block-label, metadata and attribute-group numbering."""
    line = resolve_attrs(line, groups)
    line = re.sub(r"^\d+:", "L:", line.strip())
    line = re.sub(r"%\d+", "%v", line)
    line = re.sub(r"!\d+", "!M", line)
    return re.sub(r"\s+", " ", line).strip()


def without_attrs(lines):
    """The lines with every resolved attribute set removed."""
    return [re.sub(r"\s+", " ", ATTR_TEXT_RE.sub("", l)).strip()
            for l in lines]


def split_ir(text):
    """(function name -> body lines, module-level lines)"""
    funcs = {}
    rest = []
    cur = None
    name = None
    for line in text.splitlines():
        if cur is None:
            m = re.match(r"^define .*@([^\s(]+)\(", line)
            if m:
                name = m.group(1)
                cur = []
                rest.append(line)       # the signature is module-level text
            elif not (line.startswith("; ModuleID") or
                      line.startswith("source_filename") or
                      ATTR_GROUP_RE.match(line)):
                rest.append(line)
        elif line == "}":
            funcs[name] = cur
            cur = None
        else:
            cur.append(line)
    return funcs, rest


def classify_ir(old_text, new_text):
    """
    ir-identical; ir-attrs (only attributes differ: function, parameter,
    return and call attributes such as nounwind or noundef, in bodies and at
    module level); ir-copies (bodies equal after dropping the record-copy
    instructions); ir-module (bodies equal, module level differs beyond
    attributes); ir-other (a body differs, or the function sets differ).  The
    worst class of the file wins: other > module > copies > attrs.
    """
    ga, gb = attr_groups(old_text), attr_groups(new_text)
    fa, ma = split_ir(old_text)
    fb, mb = split_ir(new_text)
    if set(fa) != set(fb):
        return "ir-other"
    ma = sorted(norm_ir(l, ga) for l in ma if l.strip())
    mb = sorted(norm_ir(l, gb) for l in mb if l.strip())
    copies = False
    attrs = False
    for name in fa:
        a = [norm_ir(l, ga) for l in fa[name] if l.strip()]
        b = [norm_ir(l, gb) for l in fb[name] if l.strip()]
        if a == b:
            continue
        if without_attrs(a) == without_attrs(b):
            attrs = True
            continue
        da = [l for l in a if not IR_MEM_RE.match(l)]
        db = [l for l in b if not IR_MEM_RE.match(l)]
        if without_attrs(da) == without_attrs(db):
            copies = True
        else:
            return "ir-other"
    result = "ir-identical"
    if ma != mb:
        result = "ir-attrs" if sorted(without_attrs(ma)) == \
            sorted(without_attrs(mb)) else "ir-module"
    if result == "ir-module":
        return result
    if copies:
        return "ir-copies"
    if attrs or result == "ir-attrs":
        return "ir-attrs"
    return result


SELF_TEST_IR = """; ModuleID = 'x'
declare void @g(ptr) #1
define void @f(ptr %0) #0 {
1:
  %2 = alloca i32
  call void @g(ptr %0) #1
  br label %3
3:
  ret void
}
attributes #0 = { noinline }
attributes #1 = { nounwind }
"""


def self_test():
    """The IR classes on one function with seven kinds of change."""
    t = SELF_TEST_IR
    cases = [
        ("ir-identical", t),
        ("ir-identical", t.replace("1:", "5:").replace("3:", "7:")
         .replace("label %3", "label %7")),
        ("ir-attrs", t.replace("call void @g(ptr %0) #1",
                               "call void @g(ptr %0) #2")
         + "attributes #2 = { }\n"),
        ("ir-attrs", t.replace("declare void @g(ptr) #1",
                               "declare void @g(ptr) #0")),
        ("ir-copies", t.replace("%2 = alloca i32\n",
                                "%2 = alloca i32\n  %9 = alloca i64\n")),
        ("ir-module", t.replace("declare void @g(ptr) #1",
                                "declare void @g(ptr) #1\ndeclare void @h()")),
        ("ir-other", t.replace("ret void", "unreachable")),
    ]
    bad = 0
    for want, new in cases:
        got = classify_ir(t, new)
        if got != want:
            print("self-test: wanted %s, got %s" % (want, got))
            bad += 1
    print("self-test: %d of %d wrong" % (bad, len(cases)))
    return 1 if bad else 0


def compile_one(tool, stream, opt, emit="asm"):
    p = subprocess.run([tool, opt, "--emit=" + emit, "--keep-going", "-o",
                        "/dev/stdout", str(stream)], capture_output=True,
                       text=True, timeout=300)
    return p.returncode, p.stdout


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("c", nargs="?")
    ap.add_argument("--opt", default="-O0")
    ap.add_argument("--streams", action="store_true")
    ap.add_argument("-j", type=int, default=4)
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    counts = collections.Counter()
    notes = collections.Counter()
    listing = collections.defaultdict(list)

    def record(name, cls, nts):
        counts[cls] += 1
        for n in nts:
            notes[n] += 1
        listing[cls].append(name)

    if args.streams:
        streams = sorted(Path(args.a).glob("*.ecil"))

        def one(s):
            ra = compile_one(args.b, s, args.opt)
            rb = compile_one(args.c, s, args.opt)
            ia = compile_one(args.b, s, args.opt, "llvm")
            ib = compile_one(args.c, s, args.opt, "llvm")
            return s, ra, rb, ia, ib

        ir_counts = collections.Counter()
        with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
            for s, ra, rb, ia, ib in ex.map(one, streams):
                if ra[0] != rb[0]:
                    record(s.name, "status", set())
                    continue
                cls, nts = classify_file(ra[1], rb[1])
                if ia[0] == 0 and ib[0] == 0:
                    icls = classify_ir(ia[1], ib[1])
                    ir_counts[icls] += 1
                    if cls != "identical" and icls == "ir-identical":
                        cls = "backend-only"
                record(s.name, cls, nts)
        for k in sorted(ir_counts):
            notes["LLVM IR " + k[3:]] += ir_counts[k]
    else:
        old, new = Path(args.a), Path(args.b)
        for g in sorted(new.glob("*.s")):
            if not (old / g.name).exists():
                record(g.name, "only-new", set())
        for f in sorted(old.glob("*.s")):
            g = new / f.name
            if not g.exists():
                record(f.name, "status", set())
                continue
            cls, nts = classify_file(f.read_text(errors="replace"),
                                     g.read_text(errors="replace"))
            record(f.name, cls, nts)
    total = sum(counts.values())
    if total == 0:
        print("compared nothing")
        return 2
    print("%d files" % total)
    for c in CLASSES:
        if counts[c]:
            print("  %-10s %6d  (%.1f%%)" % (c, counts[c],
                                              100.0 * counts[c] / total))
    for n, k in sorted(notes.items()):
        print("  note: %s: %d files" % (n, k))
    if args.v:
        for c in CLASSES[1:]:
            for name in listing[c][:50]:
                print("%s: %s" % (c, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
