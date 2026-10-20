#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""lower_to_llvm_ir against upstream's lowerDirectlyFromCIRToLLVMIR.

usage: lowering_drift_tests.py TESTDIR ECIL_ASM EDG_CIR WORKDIR

The library lowers CIR to LLVM IR with its own driver (the same passes and
translation as the upstream function, without the verifiers unless
EDG_CIR_FLAG_VERIFY_EACH).  The test hook EDG_CIR_TEST_UPSTREAM_LOWERING
makes it call the upstream function instead.  Every stream of the cirgen
tests (a `bad-` test is skipped) is compiled to LLVM IR both ways, before
any LLVM pass (--no-llvm-passes); the two texts must be identical.  A pin bump
that adds a pass or a step to the upstream function, or changes the
translation, fails here.  Exits 1 on a difference or when nothing compared.
"""
import os, re, subprocess, sys
from pathlib import Path

testdir, asm, edg, work = sys.argv[1:5]
os.makedirs(work, exist_ok=True)
compared, failures = 0, []
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
        outs = []
        for upstream in (False, True):
            env = dict(os.environ)
            if upstream:
                env["EDG_CIR_TEST_UPSTREAM_LOWERING"] = "1"
            p = subprocess.run([edg, opt, "--no-llvm-passes", "--emit=llvm",
                                str(ecil)], env=env, capture_output=True,
                               text=True)
            outs.append((p.returncode, p.stdout))
        if outs[0][0] != 0 or outs[1][0] != 0:
            if outs[0][0] != outs[1][0]:
                failures.append("%s %s: status %d against %d" %
                                (path.name, opt, outs[0][0], outs[1][0]))
            continue
        compared += 1
        if outs[0][1] != outs[1][1]:
            failures.append("%s %s: the LLVM IR differs" % (path.name, opt))
print("lowering-drift: %d lowerings compared" % compared)
for f in failures:
    print("FAIL", f)
if compared < 20:
    print("FAIL: vacuous")
    sys.exit(1)
sys.exit(1 if failures else 0)
