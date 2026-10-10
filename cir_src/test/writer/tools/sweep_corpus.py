#!/usr/bin/env python3
"""Write the M2 writer-sweep corpus list: the rp/lp tests of
tests/tests/imported/gnu/c (top level, torture/, tree-ssa/, ipa/) with their
//options: lines, as "path<TAB>options".  Usage: sweep_corpus.py [OUT]"""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
base = os.path.join(here, '../../../../tests/tests/imported/gnu/c')
out = []
for root, dirs, files in os.walk(base):
    rel = os.path.relpath(root, base)
    top = rel == '.'
    if not top and rel.split('/')[0] not in ('torture', 'tree-ssa', 'ipa'):
        continue
    for f in sorted(files):
        if not f.endswith('.c'):
            continue
        head = open(os.path.join(root, f), errors='replace').read(600)
        m = re.search(r'^//type:\s*(\w+)', head, re.M)
        if m and m.group(1) in ('rp', 'lp'):
            o = re.search(r'^//options:(.*)$', head, re.M)
            out.append((f if top else os.path.join(rel, f),
                        o.group(1).strip() if o else ''))
dest = open(sys.argv[1], 'w') if len(sys.argv) > 1 else sys.stdout
for p, o in sorted(out):
    dest.write('%s\t%s\n' % (p, o))
