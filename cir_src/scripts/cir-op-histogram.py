#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Aggregate the per-function op differences of cir-compare.py reports.

usage: cir-op-histogram.py [--top N] [--examples] FILE.cmp...

Reads the output of cir-compare.py --cxx --fe (box_diff.sh, or the M4
probe runs): every `ops: clang only xN: <op>` / `ops: edg-cir only xN: <op>`
line of a DIFF function.  The op is reduced to its dialect name (`cir.call`
of a callee keeps the callee, `cir.cast` its kind), and the table lists, per
side, how many functions and programs show each op as a difference, and the
total count.  Also lists the functions whose call sequence differs, the
functions and globals that exist on one side only, and the DIFF type/global
lines.  Ops that only change place are not shown (the multiset is equal).
"""
import argparse
import collections
import os
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument("files", nargs="+")
ap.add_argument("--top", type=int, default=40)
ap.add_argument("--examples", action="store_true",
                help="print one full op line per row")
args = ap.parse_args()


def reduce_op(text):
    """cir.cast bitcast % : ... -> cir.cast bitcast; cir.call @f(...) ->
    cir.call @f; others to the op name."""
    t = text.strip()
    m = re.match(r"(cir\.call(?:_indirect)?) (@[\w.$]+)", t)
    if m:
        return "%s %s" % (m.group(1), m.group(2))
    m = re.match(r"(cir\.cast) (\w+)", t)
    if m:
        return "%s %s" % (m.group(1), m.group(2))
    m = re.match(r"(cir\.get_global) (@[\w.$]+)", t)
    if m:
        return "cir.get_global"
    m = re.match(r"(cir\.\w+(?:\.\w+)*)", t)
    if m:
        return m.group(1)
    if t.startswith("}") or t.startswith("^"):
        return t.split("{")[0].strip()[:40]
    return t[:40]


rows = collections.defaultdict(lambda: {"count": 0, "funcs": set(),
                                        "progs": set(), "example": ""})
calls_differ = []
only = collections.defaultdict(list)
other_diffs = []
ndiff = nsame = 0
for path in args.files:
    prog = os.path.basename(path).replace(".cmp", "")
    cur = None
    for line in open(path, errors="replace"):
        m = re.match(r"^(DIFF|same)  (func|global|type) (\S+)", line)
        if m:
            cur = None
            if m.group(2) == "func":
                if m.group(1) == "same":
                    nsame += 1
                else:
                    ndiff += 1
                    cur = m.group(3)
            elif m.group(1) == "DIFF":
                other_diffs.append("%s: %s %s" % (prog, m.group(2),
                                                  m.group(3)))
            continue
        m = re.match(r"^only in (clang|edg-cir): (.*)$", line)
        if m:
            only[m.group(1)].append("%s: %s" % (prog, m.group(2)[:100]))
            continue
        if cur is None:
            continue
        m = re.match(r"^  ops: (clang|edg-cir) only x(\d+): (.*)$", line)
        if m:
            side, n, op = m.group(1), int(m.group(2)), m.group(3)
            key = (side, reduce_op(op))
            r = rows[key]
            r["count"] += n
            r["funcs"].add((prog, cur))
            r["progs"].add(prog)
            if not r["example"]:
                r["example"] = op[:150]
        elif line.startswith("  calls: DIFFER"):
            calls_differ.append("%s %s: %s" % (prog, cur, line.strip()[:150]))

print("functions: %d same, %d differ (%d programs)" % (
    nsame, ndiff, len(args.files)))
for side in ("edg-cir", "clang"):
    print("\n== ops only in %s (reduced op, total, functions, programs)" %
          side)
    items = sorted(((k[1], v) for k, v in rows.items() if k[0] == side),
                   key=lambda kv: -len(kv[1]["funcs"]))
    for op, v in items[:args.top]:
        print("%-52s %5d %5d %4d%s" % (op[:52], v["count"], len(v["funcs"]),
                                       len(v["progs"]),
                                       ("   e.g. " + v["example"])
                                       if args.examples else ""))
print("\n== call sequences that differ: %d" % len(calls_differ))
for l in calls_differ[:60]:
    print("  " + l)
for side in ("clang", "edg-cir"):
    print("\n== only in %s: %d" % (side, len(only[side])))
    for l in only[side][:60]:
        print("  " + l)
print("\n== DIFF type/global: %d" % len(other_diffs))
for l in other_diffs[:60]:
    print("  " + l)
