#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Structural op comparison of edg-cir's CIR against clang's, per function.

usage: cir-op-compare.py [--top N] [--by-program] DUMPDIR...

Each DUMPDIR holds edg-cir.txt and clang.txt written by
`cir-compare.py --dump DIR` (normalized items, '#### func @name' headers).
For every function present on both sides it counts the *material* ops
(calls by callee, vtable/vtt/base-class ops, EH ops and cleanup kinds, array
loops, member-pointer ops, memcpy/memset, loops and switches; allocas, loads,
stores, constants, casts, branches, yields and returns are plumbing and are
not counted) and reports, summed over all functions, the ops one side has more
of, with the number of functions and programs.  Naming and order do not
matter.  Functions on one side only are listed separately.
"""
import argparse
import collections
import os
import re

ap = argparse.ArgumentParser()
ap.add_argument("dirs", nargs="+")
ap.add_argument("--top", type=int, default=60)
ap.add_argument("--by-program", action="store_true")
ap.add_argument("--categorize", action="store_true",
                help="sort the differing functions into the known "
                "categories (cxx_cir_differential.md and m4_stress "
                "follow-up) and list the ones no rule explains")
ap.add_argument("--groups", type=int, default=0, metavar="N",
                help="also list the N most common whole-function deltas "
                "(the set of ops by which a function differs), with one "
                "example each")
ap.add_argument("--plumbing", action="store_true",
                help="count every op, not only the material ones")
ap.add_argument("--keep-noise", action="store_true",
                help="also count cir.scope, cir.unreachable, cir.trap, "
                "cir.if, cir.ternary, cir.ptr_stride, cir.get_member and "
                "cir.get_element (shapes of the same thing, see "
                "cxx_cir_differential.md)")
ap.add_argument("--keep-d0", action="store_true",
                help="do not skip the functions whose clang body is a "
                "cir.trap stub")
args = ap.parse_args()

MATERIAL = {
    "cir.vtable.address_point", "cir.vtable.get_vptr",
    "cir.vtable.get_virtual_fn_addr", "cir.vtable.get_type_info",
    "cir.vtt.address_point", "cir.base_class_addr", "cir.derived_class_addr",
    "cir.dyn_cast", "cir.try", "cir.throw", "cir.begin_catch",
    "cir.end_catch", "cir.init_catch_param", "cir.construct_catch_param",
    "cir.alloc.exception", "cir.eh.terminate", "cir.resume",
    "cir.begin_cleanup", "cir.end_cleanup", "cir.cleanup.scope",
    "cir.array.ctor", "cir.array.dtor", "cir.delete_array", "cir.get_method",
    "cir.derived_method", "cir.base_method", "cir.base_data_member",
    "cir.derived_data_member", "cir.get_runtime_member", "cir.copy",
    "cir.libc.memcpy", "cir.libc.memset", "cir.libc.memmove", "cir.switch",
    "cir.while", "cir.do", "cir.for", "cir.mul.overflow", "cir.add.overflow",
    "cir.select", "cir.local_init", "cir.get_global", "cir.ternary",
    "cir.if", "cir.scope", "cir.goto", "cir.label", "cir.unreachable",
    "cir.trap", "cir.eh.dispatch", "cir.eh.inflight_exception",
    "cir.catch_param", "cir.vtable.get_vptr", "cir.get_member",
    "cir.base_class_addr", "cir.ptr_stride", "cir.get_element",
}
NOISE = {"cir.scope", "cir.unreachable", "cir.trap", "cir.if", "cir.ternary",
         "cir.ptr_stride", "cir.get_member", "cir.get_element"}


def fold_variants(sym):
    """C1/C2/C5 -> Cx and D1/D2/D5 -> Dx in the nested name of an Itanium
    symbol (D0 stays: the deleting destructor is compared as such)."""
    if sym.startswith("_ZZ"):
        # a member of a local class: _ZZ<function>E<nested-name-ish>: fold the
        # last C1E/D1E-like component
        m = list(re.finditer(r"([CD])([12345])E(?=[vi0-9PRO]|$)", sym))
        if m:
            k = m[-1]
            return sym[:k.start(2)] + "x" + sym[k.end(2):]
        return sym
    if not sym.startswith("_ZN"):
        return sym
    out = list(sym)
    i = 3
    depth = 0
    n = len(sym)
    while i < n:
        c = sym[i]
        if c.isdigit():
            j = i
            while j < n and sym[j].isdigit():
                j += 1
            i = j + int(sym[i:j])
            continue
        if c in "INLXJ":
            depth += 1
        elif c == "E":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and c in "CD" and i + 1 < n and \
                sym[i + 1] in "12345":
            out[i + 1] = "x"
            i += 2
            continue
        i += 1
    return "".join(out)


D0_NOISE = {"cir.try", "cir.eh.terminate", "} catch all"}


def labels(block, name=""):
    """The material op labels of one function block."""
    out = collections.Counter()
    for raw in block.split("\n"):
        l = re.sub(r"^%[\w.#]+(, %[\w.#]+)* = ", "", raw.strip())
        m = re.match(r"cir\.call (@[\w.$]+)\(", l)
        if m:
            out["call @" + fold_variants(m.group(1)[1:])] += 1
            continue
        if re.match(r"cir\.call(_indirect)? %", l) or l.startswith(
                "cir.call_indirect"):
            out["call indirect"] += 1
            continue
        m = re.match(r"\} (cleanup (?:normal|all|eh)|catch all|catch \[type"
                     r"|unwind|unexpected|catch)", l)
        if m:
            out["} " + m.group(1)] += 1
            continue
        m = re.match(r"(cir\.[\w.]+)", l)
        if m:
            op = m.group(1)
            if op in NOISE and not args.keep_noise:
                continue
            if args.plumbing or op in MATERIAL:
                if op == "cir.get_global":
                    g = re.match(r"cir\.get_global (@[\w.$\"]+)", l)
                    op = "cir.get_global " + (g.group(1) if g else "")
                out[op] += 1
    if name.endswith("D0Ev") and not args.keep_noise:
        # clang wraps the deleting destructor in try/catch-all terminate
        # (it is noexcept); nothing in D0 can throw (D1 is noexcept itself).
        for k in D0_NOISE:
            out.pop(k, None)
    return out


def resolve_aliases(items):
    """A function declared `alias(@x)` has the body of x."""
    out = dict(items)
    for key, text in items.items():
        if key[0] != "func":
            continue
        head = text.split("\n", 1)[0]
        m = re.search(r"\balias\(@([\w.$]+)\)", head)
        if m and ("func", m.group(1)) in items:
            out[key] = items[("func", m.group(1))]
    return out


def read(path):
    items = {}
    cur = None
    buf = []
    if not os.path.exists(path):
        return items
    for line in open(path, errors="replace"):
        m = re.match(r"^#### (\w+) @(.*)$", line.rstrip("\n"))
        if m:
            if cur:
                items[cur] = "\n".join(buf)
            cur = (m.group(1), m.group(2))
            buf = []
        else:
            buf.append(line.rstrip("\n"))
    if cur:
        items[cur] = "\n".join(buf)
    return resolve_aliases(items)


def category(delta, fname):
    """The known category of a whole-function delta, or None."""
    d = dict(delta)
    labs = set(d)
    plus = {k for k, n in d.items() if n > 0}
    minus = {k for k, n in d.items() if n < 0}

    def only(allowed, signs=None):
        return all(re.match(allowed, k) for k in labs)

    if all(k.startswith("cir.get_global @") for k in labs) and not plus:
        return "clang: dead get_global of the callee (varargs/builtin calls)"
    if labs == {"cir.base_class_addr"} and not plus:
        return "clang: dead base_class_addr of an unused base subobject"
    if re.search(r"D[12]Ev$", fname) and not minus - {"cir.base_class_addr"} \
            and plus and all(k in ("cir.vtable.address_point",
                                   "cir.vtable.get_vptr",
                                   "cir.vtt.address_point",
                                   "cir.base_class_addr",
                                   "cir.get_global @printf") for k in labs):
        return "edg-cir: vptr reset in the destructor (the pin omits it)"
    if labs <= {"cir.libc.memset"} | {k for k in labs if k.startswith(
            "call @") and k.endswith("Ev")} and "cir.libc.memset" in plus:
        return "edg-cir: zeroed base subobject is a memset (clang: const store)"
    if all(re.match(r"call @_ZSt(7forward|4move|11__addressof|10__invoke_r|"
                    r"6invoke|13__invoke_impl|23__is_constant_evaluated)",
                    k) for k in labs):
        return "edg-cir: calls std::forward/move/addressof (clang folds them)"
    if any(re.match(r"call @.*C[x]E", k) for k in minus) and not (
            plus - {"cir.vtable.address_point", "cir.vtable.get_vptr",
                    "cir.get_global @printf"}):
        return ("clang: constructor call where the front end constant-"
                "initialized a trivial dynamic class")
    if "call @__cxa_free_exception" in plus:
        return "edg-cir: frees the exception object if its construction "\
               "throws (the pin leaks it)"
    return None


more_edg = collections.defaultdict(lambda: [0, set(), set()])
more_clang = collections.defaultdict(lambda: [0, set(), set()])
only = {"edg-cir": [], "clang": []}
stubs = []
cats = collections.defaultdict(list)
groups = collections.defaultdict(list)
decl_only = []
nfunc = nsame = 0
per_prog = {}
for d in args.dirs:
    prog = os.path.basename(d.rstrip("/"))
    a = read(os.path.join(d, "edg-cir.txt"))
    b = read(os.path.join(d, "clang.txt"))
    if not a or not b:
        continue
    pd = 0
    for key in sorted(set(a) & set(b)):
        if key[0] != "func":
            continue
        if not args.keep_d0 and "cir.trap" in b[key] and \
                "cir.try" in b[key] and len(b[key].split("\n")) < 16:
            stubs.append("%s %s" % (prog, key[1]))
            continue
        if len(a[key].split("\n")) <= 2 or len(b[key].split("\n")) <= 2:
            # one side only declares it (clang defers inline functions)
            decl_only.append("%s %s (%s)" % (
                prog, key[1], "clang declares" if len(b[key].split("\n")) <= 2
                else "edg-cir declares"))
            continue
        nfunc += 1
        la, lb = labels(a[key], key[1]), labels(b[key], key[1])
        if la == lb:
            nsame += 1
            continue
        pd += 1
        delta = tuple(sorted(
            (k, la[k] - lb[k]) for k in set(la) | set(lb) if la[k] != lb[k]))
        groups[delta].append((prog, key[1]))
        cats[category(delta, key[1])].append((prog, key[1], delta))
        for k in set(la) | set(lb):
            n = la[k] - lb[k]
            if n > 0:
                r = more_edg[k]
            elif n < 0:
                r = more_clang[k]
            else:
                continue
            r[0] += abs(n)
            r[1].add((prog, key[1]))
            r[2].add(prog)
    for key in sorted(set(a) - set(b)):
        if key[0] == "func" and len(a[key].split("\n")) > 2:
            only["edg-cir"].append("%s %s" % (prog, key[1]))
    for key in sorted(set(b) - set(a)):
        if key[0] == "func" and len(b[key].split("\n")) > 2:
            only["clang"].append("%s %s" % (prog, key[1]))
    per_prog[prog] = (len([k for k in set(a) & set(b) if k[0] == "func"]), pd)

print("functions where clang's body is a cir.trap stub (not compared): %d"
      % len(stubs))
print("functions one side only declares (not compared): %d" % len(decl_only))
print("functions in both: %d, equal material op multiset: %d, differ: %d "
      "(%d programs)" % (nfunc, nsame, nfunc - nsame, len(per_prog)))
for title, table in (("ops edg-cir has MORE of than clang", more_edg),
                     ("ops clang has MORE of than edg-cir", more_clang)):
    print("\n== %s: op, extra count, functions, programs" % title)
    for k, v in sorted(table.items(), key=lambda kv: -len(kv[1][1]))[
            :args.top]:
        print("%-58s %6d %5d %4d" % (k[:58], v[0], len(v[1]), len(v[2])))
if args.categorize:
    print("\n== differing functions by category")
    for c, fns in sorted(cats.items(), key=lambda kv: -len(kv[1])):
        if c is not None:
            print("%5d  %s" % (len(fns), c))
    rest = cats.get(None, [])
    print("%5d  not explained by a rule:" % len(rest))
    for prog, fn, delta in rest[:200]:
        print("   %s %s: %s" % (prog, fn[:50], ", ".join(
            "%s%+d" % (k[:40], n) for k, n in delta)[:200]))
if args.groups:
    print("\n== most common whole-function deltas (+ edg-cir has more, "
          "- clang has more): functions, programs, example")
    for delta, fns in sorted(groups.items(), key=lambda kv: -len(kv[1]))[
            :args.groups]:
        print("%4d fn %3d prog  e.g. %s %s" % (
            len(fns), len({f[0] for f in fns}), fns[0][0], fns[0][1][:60]))
        print("        " + ", ".join("%s%+d" % (k[:44], n)
                                     for k, n in delta)[:260])
for side in ("edg-cir", "clang"):
    print("\n== function definitions only in %s: %d" % (side, len(only[side])))
    for l in only[side][:args.top]:
        print("  " + l[:130])
if args.by_program:
    print("\n== per program: functions compared, functions that differ")
    for p, (n, d) in sorted(per_prog.items()):
        print("%-34s %4d %4d" % (p, n, d))
