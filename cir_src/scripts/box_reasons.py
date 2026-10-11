#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env: why the C++ corpus cases (box_corpus.py, default mode) that
fail with "was dropped" were dropped: the front end's own warnings.
usage: box_reasons.py OUTNAME  -> prints reason counts (cases per reason)."""
import re, sys, glob, collections
out = "build/scratch/corpus-" + sys.argv[1]
cases = collections.defaultdict(set)
for lib in glob.glob(out + "/*.lib"):
    t = open(lib, errors="replace").read()
    if "was dropped" not in t:
        continue
    fe = lib[:-4] + ".fe"
    f = open(fe, errors="replace").read()
    for m in re.finditer(r'warning: CIR back end: (the body of "[^"]*" uses|the initializer of[^\n]*?uses|[^\n]*?(?:uses|is))\s+(.*?)(?:, which the CIR back end|;)', re.sub(r"\s*\n\s*", " ", f)):
        why = re.sub(r"\s+", " ", m.group(2))[:110]
        cases[why].add(lib.split("/")[-1])
for why, s in sorted(cases.items(), key=lambda kv: -len(kv[1])):
    print("%4d %s   e.g. %s" % (len(s), why, sorted(s)[0].split("_",1)[1][:-8]))
