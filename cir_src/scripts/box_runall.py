#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env: every corpus case (C and C++) through front end, library,
link and run, with the front end options of the real run (the eccp command of
the checkpoint).  Run-and-print tests only; the program's output is compared
with the recorded expectation (.NAME.rto/default.K.2.txt, K = TEST_NUMBER).

input  build/scratch/cmds.tsv: path, eccp command, variant, status (tab)
output build/scratch/runall-OUTNAME.tsv: path variant class detail
classes: FE_DIAG (front end error: stream aborted), FE_NONE, LIB_CRASH,
LIB_ERROR, LIB_UNSUPPORTED, LINK_FAIL, RUN_TIMEOUT, RUN_CRASH (rc>=128),
RUN_RC (rc!=0, output equal), RUN_DIFF (output differs), RUN_OK, NOT_RP
usage: box_runall.py OUTNAME [KEEP_GOING]"""
import os, re, subprocess, sys, shlex, collections
from concurrent.futures import ThreadPoolExecutor

W = os.getcwd()
name = sys.argv[1]
keep = ["--keep-going"] if len(sys.argv) > 2 else []
out = "build/scratch/runall-" + name
os.makedirs(out, exist_ok=True)
env = dict(os.environ, EDG_CIR_RT_LIBDIR=W + "/build/cir/lib",
           LD_LIBRARY_PATH=W + "/build/cir/lib")
rows = [l.rstrip("\n").split("\t") for l in open("build/scratch/cmds.tsv")]


def sh(cmd, timeout, cwd=None):
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=timeout, env=env,
                           cwd=cwd)
        return r.returncode, (r.stdout + r.stderr).decode("latin-1")
    except subprocess.TimeoutExpired:
        return 124, ""


def expected(path, variant):
    d, f = os.path.split("tests/tests/" + path)
    base = re.sub(r"\.(sft|mft)\.(cpp|c)$", "", f)
    m = re.search(r"-DTEST_NUMBER=(\d+)", variant)
    k = m.group(1) if m else "1"
    p = "%s/.%s.rto/default.%s.2.txt" % (d, base, k)
    if not os.path.exists(p):
        return None
    t = open(p, errors="replace").read()
    return t.split("\n", 1)[1] if "\n" in t else ""


def run(ic):
    i, (path, cmd, variant, status) = ic
    src = "tests/tests/" + path
    first = open(src, errors="replace").readline()
    if not re.match(r"//\s*type:\s*rp", first):
        return (path, variant, "NOT_RP", "")
    toks = shlex.split(cmd)
    flags = [t for t in toks[1:] if t != "Test_name.c"]
    n = "%d_%s" % (i, os.path.basename(path))
    ecil = "%s/%s.ecil" % (out, n)
    exe = "%s/%s.exe" % (out, n)
    for f in (ecil,):
        if os.path.exists(f):
            os.remove(f)
    rc, o = sh([W + "/build/gcc-cir/bin/cpfe-cir", "--edg_base",
                W + "/bases/docker/cir-env/gcc"] + flags +
               ["--ecil_output=" + ecil, src], 120)
    if not os.path.exists(ecil) or os.path.getsize(ecil) == 0:
        return (path, variant, "FE_NONE", "rc=%d" % rc)
    rc, o = sh([W + "/build/cir/bin/edg-cir"] + keep +
               ["--emit=obj", "-o", exe + ".o", ecil], 120)
    if rc != 0:
        msg = next((l for l in o.split("\n") if re.search("error|crash|Assertion", l)), "")[:200]
        if "aborted the ECIL stream" in o or "stream was aborted" in o:
            return (path, variant, "FE_DIAG", "")
        if re.search("internal crash|internal error|Assertion|Aborted", o):
            return (path, variant, "LIB_CRASH", msg)
        return (path, variant, "LIB_ERROR", msg)
    rc, o = sh(["g++", "-no-pie", exe + ".o", "-o", exe, "-lm", "-lpthread"],
               60)
    if rc != 0:
        return (path, variant, "LINK_FAIL", re.sub(r"\s+", " ", o)[:200])
    d = out + "/" + n + ".d"
    os.makedirs(d, exist_ok=True)
    rc, o = sh([os.path.abspath(exe)], 20, cwd=d)
    for f in (exe, exe + ".o"):
        if os.path.exists(f):
            os.remove(f)
    if rc == 124:
        return (path, variant, "RUN_TIMEOUT", "")
    exp = expected(path, variant)
    open(out + "/" + n + ".out", "w").write(o)
    open(out + "/" + n + ".exp", "w").write(exp or "")
    def norm(t):
        return re.sub(r"0x[0-9a-fA-F]+", "0xADDR", t).strip()
    same = exp is not None and norm(o) == norm(exp)
    if rc >= 128 or rc < 0:
        return (path, variant, "RUN_CRASH", "rc=%d" % rc)
    if exp is not None and not same:
        return (path, variant, "RUN_DIFF", re.sub(r"\s+", " ", o)[:120])
    if rc != 0:
        return (path, variant, "RUN_RC", "rc=%d" % rc)
    return (path, variant, "RUN_OK", "")


with ThreadPoolExecutor(14) as ex:
    res = list(ex.map(run, enumerate(rows)))
with open("build/scratch/runall-%s.tsv" % name, "w") as f:
    for r in sorted(res):
        f.write("\t".join(r) + "\n")
for k, v in collections.Counter(r[2] for r in res).most_common():
    print("%6d %s" % (v, k))
