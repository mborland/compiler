#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env, from the tree root: build and run corpus tests at an
optimization level through the box front end and edg-cir (no edgy).

usage: [EDG_CIR_BIN=path] box_run_opt.py OUTDIR OPT FILTERFILE [--cxx]
For every test of the filter list (path prefixes, as for edgy) the streams of
each case are collected (box_collect_streams.py), compiled with edg-cir
-O<OPT> --emit=obj, linked together with g++ and the edg_cir_rt runtime, and
run; the exit status 0 is a pass.  Prints one row per test case.
"""
import collections, os, subprocess, sys
from pathlib import Path

out, opt, filt = sys.argv[1], sys.argv[2], sys.argv[3]
cxx = ["--cxx"] if "--cxx" in sys.argv else []
root = Path.cwd()
edg = Path(os.environ.get("EDG_CIR_BIN", root / "build/cir/bin/edg-cir"))
lib = edg.resolve().parent.parent / "lib"
env = dict(os.environ, LD_LIBRARY_PATH=str(lib), EDG_CIR_RT_LIBDIR=str(lib))
subprocess.run([sys.executable, "cir_src/scripts/box_collect_streams.py", out,
                "@" + filt, "-j", "8"] + cxx, check=True,
               stdout=subprocess.DEVNULL)
cases = collections.defaultdict(list)
for line in Path(out, "index.tsv").read_text().splitlines():
    name, test, case = line.split("\t")
    if name != "-":
        cases[(test, case)].append(name)
passed = 0
for (test, case), names in sorted(cases.items()):
    objs = []
    status = "PASS"
    for n in names:
        obj = Path(out, n + f".O{opt}.o")
        p = subprocess.run([str(edg), f"-O{opt}",
                            "--emit=obj", "-o", str(obj), str(Path(out, n))],
                           env=env, capture_output=True, text=True)
        if p.returncode != 0:
            status = "LIB-FAIL " + (p.stderr.strip().splitlines() or [""])[0][:100]
            break
        objs.append(str(obj))
    if status == "PASS":
        exe = Path(out, f"{test}.{case}.O{opt}.exe")
        p = subprocess.run(["g++", "-no-pie", *objs, "-o", str(exe),
                            f"-L{lib}", "-ledg_cir_rt", "-lm"],
                           capture_output=True, text=True)
        if p.returncode != 0:
            status = "LINK-FAIL " + (p.stderr.strip().splitlines() or [""])[0][:100]
        else:
            try:
                r = subprocess.run([str(exe)], capture_output=True, timeout=30)
                status = "PASS" if r.returncode == 0 else f"RUN rc={r.returncode}"
            except subprocess.TimeoutExpired:
                status = "RUN timeout"
    passed += status == "PASS"
    print(f"{status:40s} -O{opt} {test}.{case}")
print(f"{passed} of {len(cases)} pass at -O{opt}")
