#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env, from a tree root: chunked against whole-module lowering of
the streams of build/streams/DIR at -O<LEVEL> (the streams are made by the
front end; the corpus sets of chunk-identity run at -O0 only).

usage: box_chunk_o2.py DIR LEVEL [COUNT [SKIP]]

Each stream is compiled to assembly with `--chunk-threshold=4294967295` and
with `--chunk-functions=1 --chunk-threshold=1`; the status, the diagnostics
and the assembly must be the same.  Prints the totals and the differences.
"""
import concurrent.futures, os, re, subprocess, sys
from pathlib import Path

d, level = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
skip = int(sys.argv[4]) if len(sys.argv) > 4 else 0
edg = "build/cir/bin/edg-cir"
env = dict(os.environ, LD_LIBRARY_PATH="build/cir/lib")
streams = sorted(Path("build/streams", d).glob("*.ecil"))[skip:skip + count]


def compile_with(stream, extra):
    p = subprocess.run([edg, "-O" + level, "--emit=asm"] + extra +
                       [str(stream)], capture_output=True, env=env,
                       timeout=300)
    return p.returncode, p.stdout, p.stderr


def unnumbered(text):
    """The assembly without the numeric suffix of local data symbols that an
    LLVM pass names (`.L.crctable.1`, `.Lswitch.table.f.3`): LLVM's symbol
    table counts the collisions of the whole module, and the chunks' links
    count more."""
    return re.sub(rb"(\.L\.?[A-Za-z_][\w$.]*?)\.\d+\b", rb"\1.N", text)


def work(stream):
    a = compile_with(stream, ["--chunk-threshold=4294967295"])
    b = compile_with(stream, ["--chunk-functions=1", "--chunk-threshold=1"])
    if a[0] != b[0] or a[2] != b[2]:
        return stream.name, "status/diagnostics", a[0], b[0]
    if a[0] == 0 and a[1] != b[1]:
        if unnumbered(a[1]) == unnumbered(b[1]):
            return stream.name, "same-modulo-suffix", 0, 0
        return stream.name, "assembly", 0, 0
    return stream.name, "same" if a[0] == 0 else "same-nonrun", a[0], b[0]


tally, diffs = {}, []
with concurrent.futures.ThreadPoolExecutor(8) as ex:
    for name, kind, ra, rb in ex.map(work, streams):
        tally[kind] = tally.get(kind, 0) + 1
        if kind in ("status/diagnostics", "assembly"):
            diffs.append("%s: %s (%d, %d)" % (name, kind, ra, rb))
for line in diffs:
    print(line)
print(tally)
sys.exit(1 if diffs else 0)
