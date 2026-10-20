#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env, from a tree root: -O2 run differential of two libraries.

usage: box_o2_diff.py BUILD_A BUILD_B DIR JOBS [COUNT [SKIP]]

Every stream of build/streams/DIR (the streams of the single-file cases, made
at -O0 by the front end) is compiled with `BUILD/bin/edg-cir -O2` of each
build, linked with g++ and the runtime, and run (5 s).  Streams on which a
build fails to compile or link are skipped when both fail alike.  The exit
status and the standard output of the two programs must agree; a stream that
passes in A and fails or differs in B is listed.  Exit 1 when one is listed.
"""
import collections, concurrent.futures, os, subprocess, sys, tempfile
from pathlib import Path

a, b, d, jobs = sys.argv[1:5]
count = int(sys.argv[5]) if len(sys.argv) > 5 else 10**9
skip = int(sys.argv[6]) if len(sys.argv) > 6 else 0
streams = sorted(Path("build/streams", d).glob("*.ecil"))[skip:skip + count]

def build_and_run(build, stream, tmp):
    env = dict(os.environ, LD_LIBRARY_PATH=build + "/lib")
    obj = tmp + "/o.o"
    exe = tmp + "/o.exe"
    p = subprocess.run([build + "/bin/edg-cir", "-O2", "--emit=obj", "-o", obj, str(stream)],
                       capture_output=True, env=env, timeout=120)
    if p.returncode:
        return ("compile-fail", p.returncode, b"")
    p = subprocess.run(["g++", "-o", exe, obj, build + "/lib/libedg_cir_rt.a"], capture_output=True)
    if p.returncode:
        return ("link-fail", p.returncode, b"")
    try:
        r = subprocess.run([exe], capture_output=True, timeout=5, stdin=subprocess.DEVNULL)
        return ("ran", r.returncode, r.stdout[:100000])
    except subprocess.TimeoutExpired:
        return ("timeout", -1, b"")

def work(stream):
    tmp = tempfile.mkdtemp(prefix="o2d")
    try:
        ra = build_and_run(a, stream, tmp)
        rb = build_and_run(b, stream, tmp)
    finally:
        subprocess.run(["rm", "-rf", tmp])
    if ra == rb:
        return stream, ("same" if ra[0] == "ran" else "same-nonrun:" + ra[0])
    return stream, "DIFF A=%s/%s B=%s/%s%s" % (ra[0], ra[1], rb[0], rb[1],
                                              "" if ra[2] == rb[2] else " (output differs)")

c = collections.Counter()
bad = 0
with concurrent.futures.ThreadPoolExecutor(int(jobs)) as ex:
    for stream, verdict in ex.map(work, streams):
        c[verdict.split()[0].split(":")[0]] += 1
        if verdict.startswith("DIFF"):
            bad += 1
            print(stream.name, verdict)
print(dict(c))
sys.exit(1 if bad else 0)
