#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env: C corpus cases (build/scratch/ccorpus.tsv: path<TAB>options)
through front end, library, link and run (exit status 0 = pass).
usage: box_ccorpus.py OUTNAME"""
import subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor
name = sys.argv[1]
out = "build/scratch/ccorpus-" + name
cases = [l.rstrip("\n").split("\t") for l in open("build/scratch/ccorpus.tsv")]
def run(ic):
    i, c = ic
    r = subprocess.run(["bash", "cir_src/scripts/box_c1.sh", out, str(i), c[0]]
                       + (c[1].split() if len(c) > 1 else []),
                       capture_output=True, text=True)
    return r.stdout.strip() or "%s\tHARNESS\t%s" % (c[0], r.stderr[:100])
with ThreadPoolExecutor(14) as ex:
    rows = list(ex.map(run, enumerate(cases)))
open(out + ".tsv", "w").write("\n".join(sorted(rows)) + "\n")
for k, v in collections.Counter(r.split("\t")[1] for r in rows).most_common():
    print("%6d %s" % (v, k))
