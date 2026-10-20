#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Chunked lowering against the whole-module lowering, on the cirgen tests.

usage: chunk_identity_tests.py TESTDIR ECIL_ASM EDG_CIR WORKDIR

Every stream of the cirgen tests (a `bad-` test is skipped) is compiled to
assembly at -O0 and -O2 twice: as one module (--chunk-threshold=4294967295)
and with chunking forced on and one function body per chunk
(--chunk-functions=1 --chunk-threshold=1).  The exit status, the
diagnostics and the assembly must be identical.  Each chunked run must say
so (--stats: "chunked lowering: N chunks", N of at least 2 when the module
has a function body), the whole-module one must report 0 chunks.  The LLVM IR
before any LLVM pass (--no-llvm-passes) is compared too, as the sorted set of
its definitions, global variables, aliases and comdats (the order of the
declarations may differ, and the attribute group numbers): the linkage,
dso_local, comdat and alias of every symbol must have come back right from
the merge.  Exits 1 on a difference or when too little was compared.
"""
import os, re, subprocess, sys
from pathlib import Path

testdir, asm, edg, work = sys.argv[1:5]
os.makedirs(work, exist_ok=True)
CHUNKS = re.compile(r"stats: chunked lowering: (\d+) chunks of at most (\d+)")
compared, chunked_runs, failures = 0, 0, []


def definitions(text):
    """The sorted definitions, variables, aliases and comdats of an IR text."""
    keep = [re.sub(r" #\d+", "", l) for l in text.splitlines()
            if l.startswith(("define ", "@", "$"))]
    return sorted(keep)


def run(ecil, opt, extra):
    p = subprocess.run([edg, opt, "--emit=asm", "--stats"] + extra +
                       [str(ecil)], capture_output=True, text=True)
    m = CHUNKS.search(p.stderr)
    # (LeakSanitizer's "Unable to get registers from thread" is a flaky
    # ptrace note of the sanitizer in a container: not a diagnostic.)
    notes = [l for l in p.stderr.splitlines()
             if "stats: " not in l and "Unable to get registers" not in l]
    return p.returncode, p.stdout, notes, int(m.group(1)) if m else -1


for path in sorted(Path(testdir).glob("*.test")):
    if path.name.startswith("bad-"):
        continue
    text = path.read_text()
    m = re.search(r"RUN: cat %S/Inputs/(\S+) %s", text)
    if m:
        stream_text = Path(testdir, "Inputs", m.group(1)).read_text() + text
    elif text.startswith("ecil"):
        stream_text = text
    else:
        continue
    src = Path(work, path.stem + ".txt")
    src.write_text(stream_text)
    ecil = Path(work, path.stem + ".ecil")
    if subprocess.run([asm, str(src), "-o", str(ecil)],
                      capture_output=True).returncode != 0:
        continue
    for opt in ("-O0", "-O2"):
        whole = run(ecil, opt, ["--chunk-threshold=4294967295"])
        forced = run(ecil, opt, ["--chunk-functions=1",
                                 "--chunk-threshold=1"])
        name = "%s %s" % (path.name, opt)
        if whole[3] != 0:
            failures.append("%s: the whole-module run reports %d chunks" %
                            (name, whole[3]))
        if whole[0] != forced[0] or whole[2] != forced[2]:
            failures.append("%s: status %d against %d or different "
                            "diagnostics (whole %s, chunked %s)" %
                            (name, whole[0], forced[0], whole[2][:3],
                             forced[2][:3]))
            continue
        if whole[0] != 0:
            continue
        compared += 1
        chunked_runs += forced[3] >= 2
        if whole[1] != forced[1]:
            failures.append("%s: the assembly differs" % name)
        ir = []
        for extra in (["--chunk-threshold=4294967295"],
                      ["--chunk-functions=1", "--chunk-threshold=1"]):
            p = subprocess.run([edg, opt, "--no-llvm-passes", "--emit=llvm"] +
                               extra + [str(ecil)], capture_output=True,
                               text=True)
            ir.append(definitions(p.stdout))
        if ir[0] != ir[1]:
            failures.append("%s: the LLVM IR differs (linkage, comdat, alias "
                            "or variables after the merge)" % name)
print("chunk-identity: %d lowerings compared, %d of them chunked" %
      (compared, chunked_runs))
for f in failures:
    print("FAIL", f)
if compared < 20 or chunked_runs < 20:
    print("FAIL: vacuous")
    sys.exit(1)
sys.exit(1 if failures else 0)
