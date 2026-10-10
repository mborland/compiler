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
args = ap.parse_args()


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout, r.stderr, r.returncode


def normalize(text):
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
theirs, err2, rc2 = run([args.clang, "-cc1", "-triple", args.triple,
                         "-fclangir", "-emit-cir", "-clangir-disable-passes",
                         args.c, "-o", "-"])
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
for key in sorted(set(a) ^ set(b)):
    print("only in %s: %s @%s" % ("edg-cir" if key in a else "clang", *key))
sys.exit(status)
