#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env: the C++ corpus through front end + library only.

usage: box_corpus.py OUTNAME   (reads build/scratch/corpus.tsv: path<TAB>options)
Writes build/scratch/corpus-OUTNAME.tsv (path, class, message) and prints the
class counts.  box_corpus1.sh runs one case.
"""
import subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor

name = sys.argv[1]
out = "build/scratch/corpus-" + name
cases = [l.rstrip("\n").split("\t") for l in open("build/scratch/corpus.tsv")]


def run(ic):
    i, c = ic
    path = c[0]
    opts = c[1].split() if len(c) > 1 else []
    r = subprocess.run(["bash", "cir_src/scripts/box_corpus1.sh", out, str(i),
                        path] + opts, capture_output=True, text=True)
    row = r.stdout.strip() or "%s\tHARNESS\t%s" % (path, r.stderr[:100])
    p, k, m = (row.split("\t") + ["", ""])[:3]
    return "%s %s\t%s\t%s" % (p, c[1] if len(c) > 1 else "", k, m)


with ThreadPoolExecutor(14) as ex:
    rows = list(ex.map(run, enumerate(cases)))
open(out + ".tsv", "w").write("\n".join(sorted(rows)) + "\n")
cnt = collections.Counter(r.split("\t")[1] for r in rows)
for k, v in cnt.most_common():
    print("%6d %s" % (v, k))
