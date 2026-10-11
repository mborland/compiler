#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Diff the CIR edg-cir emits for an ECIL stream against clang's CIRGen.

usage: cir-compare.py --ecil FILE.ecil --c FILE.c [--func NAME]...
       [--clang PATH] [--edg-cir PATH] [--triple T] [--std -std=c11]

Both sides are normalized: SSA values are renumbered in order of definition,
locations, module attributes and symbol visibility noise are dropped, and
only functions and globals present on both sides are compared.  The exit
status is 1 when a compared function differs.
"""
import argparse, difflib, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--ecil", required=True)
ap.add_argument("--c", required=True)
ap.add_argument("--func", action="append", default=[])
ap.add_argument("--clang", default="/edg/llvm-cir/bin/clang")
ap.add_argument("--edg-cir", default=os.environ.get(
    "EDG_CIR", "build/cir/bin/edg-cir"))
ap.add_argument("--triple", default="x86_64-unknown-linux-gnu")
ap.add_argument("--std", default="-std=c11")
ap.add_argument("--all", action="store_true",
                help="show the normalized text of equal functions too")
ap.add_argument("--cxx", action="store_true",
                help="C++ mode: --c is a C++ file (-std=c++17; cc1 has exceptions off by default) "
                "and the C++ normalizations below apply")
ap.add_argument("--loose", action="store_true",
                help="with --cxx, also ignore alignment(N) function "
                "attributes, packed and the explicit pad arrays of record "
                "types (they are not compared by default)")
ap.add_argument("--structors", action="store_true",
                help="treat the constructor/destructor variants C1/C2/C5 and "
                "D0/D1/D2/D5 of a symbol as one (the library emits the base "
                "variant plus an alias or a use replacement, clang chooses "
                "per linkage)")
ap.add_argument("--global-order", action="store_true",
                help="also compare the order of the globals that have a ctor "
                "region (dynamic initialization order = module order)")
args = ap.parse_args()
if args.cxx:
    args.structors = True


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout, r.stderr, r.returncode


def normalize_cxx(text):
    """C++ noise that is not part of the comparison: per-argument LLVM
    attribute dictionaries, function attribute lists, comdat/func_info
    decorations and the record type alias spelling.  alignment(N), packed
    and explicit pad arrays are compared unless --loose is given (the
    lowering inserts equal padding itself, so they are cosmetic)."""
    text = re.sub(r"\s*\{llvm\.[^{}]*\}", "", text)
    text = re.sub(r"-> \((![^(),]+)\)", r"-> \1", text)
    text = re.sub(r" attributes \{[^{}]*\}", "", text)
    text = re.sub(r"\bcomdat(\(\"[^\"]*\"\))?\s*", "", text)
    text = re.sub(r"\bfunc_info<[^\n]*?>>?\s*(?=\{|$)", "", text, flags=re.M)
    text = re.sub(r"\bno_inline\s*|\bnothrow\b\s*", "", text)
    text = re.sub(r" ast\(#cir\.var\.decl\.ast\)", "", text)
    text = re.sub(r"!rec_(\w+?)2Ebase", r"!rec_\1.base", text)
    if args.loose:
        text = re.sub(r"\balignment\(\d+\)\s*", "", text)
        text = re.sub(r", pad !cir\.array<!u8i x \d+>", "", text)
        text = re.sub(r"\bpacked ", "", text)
    return text


def structor_symbol(sym):
    """Rewrite the constructor/destructor variant of an Itanium nested name
    (_ZN ... C1E / D2Ev ...) to Cx / Dx.  Only a C[1-5] or D[0-5] that is a
    component of the nested name counts: a source name such as N::C1 is
    length-prefixed (2C1) and is skipped, template arguments are skipped."""
    if not sym.startswith("_ZN"):
        return sym
    out = list(sym)
    i = 3
    depth = 0          # open I/N/L/X ... E groups inside the nested name
    n = len(sym)
    while i < n:
        c = sym[i]
        if c.isdigit():                      # <length><identifier>
            j = i
            while j < n and sym[j].isdigit():
                j += 1
            i = j + int(sym[i:j])
            continue
        if c in "INLXJ":
            depth += 1
        elif c == "E":
            if depth == 0:
                break                        # end of the nested name
            depth -= 1
        elif depth == 0 and c in "CD" and i + 1 < n and \
                sym[i + 1] in ("12345" if c == "C" else "012345"):
            out[i + 1] = "x"
            i += 2
            continue
        i += 1
    return "".join(out)


def normalize_structors(text):
    return re.sub(r"@_Z[\w.$]+", lambda m: "@" + structor_symbol(m.group(0)[1:]),
                  text)


def normalize(text):
    if args.cxx:
        text = normalize_cxx(text)
    if args.structors:
        text = normalize_structors(text)
    text = re.sub(r"\s+loc\([^\n]*?\)(?=\s*(?:\{|$))", "", text, flags=re.M)
    text = re.sub(r" loc\(#loc\d+\)", "", text)
    text = re.sub(r"^#loc.*$\n?", "", text, flags=re.M)
    text = re.sub(r"\bdso_local\b\s*", "", text)
    text = re.sub(r" +", " ", text)
    return text


def split_items(text):
    """-> {('func'|'global', name): normalized block text}"""
    items = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"\s*cir\.(func|global)\b.*?@\"?([\w.$]+)\"?", lines[i])
        if not m:
            i += 1
            continue
        kind, name = m.group(1), m.group(2)
        depth = lines[i].count("{") - lines[i].count("}")
        block = [lines[i]]
        i += 1
        while depth > 0 and i < len(lines):
            depth += lines[i].count("{") - lines[i].count("}")
            block.append(lines[i])
            i += 1
        items[(kind, name)] = renumber("\n".join(block))
    return items


def renumber(block):
    names = {}

    def sub(m):
        key = m.group(0)
        if key not in names:
            names[key] = "%%%d" % len(names)
        return names[key]
    block = re.sub(r"%[A-Za-z_][\w.]*|%\d+", sub, block)
    block = re.sub(r"\^bb\d+", "^bb", block)
    return "\n".join(l.strip() for l in block.split("\n") if l.strip())


ours, err1, rc1 = run([args.edg_cir, "--emit=cir", "--disable-cir-passes",
                       args.ecil])
if rc1:
    sys.exit("edg-cir failed:\n" + err1)
lang = (["-x", "c++", "-std=c++17"] if args.cxx
        else [])
theirs, err2, rc2 = run([args.clang, "-cc1", "-triple", args.triple]
                        + lang + ["-fclangir", "-emit-cir",
                                  "-clangir-disable-passes", args.c, "-o",
                                  "-"])
if rc2:
    sys.exit("clang failed:\n" + err2)
a = split_items(normalize(ours))
b = split_items(normalize(theirs))
status = 0
for key in sorted(set(a) & set(b)):
    if args.func and key[1] not in args.func:
        continue
    if a[key] == b[key]:
        print("same  %s @%s" % key)
        if args.all:
            print(a[key])
    else:
        status = 1
        print("DIFF  %s @%s" % key)
        for line in difflib.unified_diff(
                b[key].split("\n"), a[key].split("\n"), "clang", "edg-cir",
                lineterm="", n=2):
            print("  " + line)
if args.global_order:
    def order(text):
        return [m.group(1) for m in re.finditer(
            r"cir\.global [^\n@]*@\"?([\w.$]+)\"?[^\n]*\bctor\b", text)]
    oa, ob = order(normalize(ours)), order(normalize(theirs))
    common = [n for n in ob if n in oa]
    if [n for n in oa if n in ob] != common:
        status = 1
        print("DIFF  global init order: clang %s, edg-cir %s" % (common,
              [n for n in oa if n in ob]))
    else:
        print("same  global init order %s" % common)
for key in sorted(set(a) ^ set(b)):
    print("only in %s: %s @%s" % ("edg-cir" if key in a else "clang", *key))
sys.exit(status)
