#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Lazy against eager emission over the streams of the cirgen lit tests.

usage: lazy_identity_tests.py TESTDIR ECIL_ASM EDG_CIR WORKDIR

Every cirgen test that is not a `bad-` test and whose input is a stream (a
file that starts with `ecil`, or a `cat %S/Inputs/<header> %s` test) is
assembled and compiled to assembly at -O0 and -O2 with lazy emission (the
default) and with --eager-bodies.  The two must agree on the exit status and
produce the same assembly byte for byte.  The streams that defer a body must
number more than 10 (the check is not vacuous).  Exits 1 on a difference.
"""
import os, re, subprocess, sys
from pathlib import Path

testdir, asm, edg, work = (sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
os.makedirs(work, exist_ok=True)
failures, compared, deferred_streams = [], 0, 0

for path in sorted(Path(testdir).glob("*.test")):
    if path.name.startswith("bad-") or path.name.startswith("lazy-identity"):
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
        results = {}
        for mode, extra in (("lazy", []), ("eager", ["--eager-bodies"])):
            out = Path(work, "%s%s.%s.s" % (path.stem, opt, mode))
            p = subprocess.run([edg, opt, "--emit=asm", "--stats", "-o",
                                str(out), str(ecil)] + extra,
                               capture_output=True, text=True)
            results[mode] = (p.returncode,
                             out.read_bytes() if p.returncode == 0 else b"",
                             p.stderr)
        compared += 1
        l, e = results["lazy"], results["eager"]
        if l[0] != e[0] and not (l[0] == 0 and e[0] != 0):
            failures.append("%s %s: status lazy %d eager %d" %
                            (path.name, opt, l[0], e[0]))
        elif l[0] == 0 and l[1] != e[1]:
            failures.append("%s %s: the assembly differs" % (path.name, opt))
        m2 = re.search(r"lazy bodies: (\d+) deferred", l[2])
        if opt == "-O0" and m2 and int(m2.group(1)) > 0:
            deferred_streams += 1

print("lazy-identity: %d compilations, %d streams defer a body" %
      (compared, deferred_streams))
for f in failures:
    print("FAIL", f)
if compared == 0 or deferred_streams <= 10:
    print("FAIL: vacuous (compared %d, deferring %d)" %
          (compared, deferred_streams))
    sys.exit(1)
sys.exit(1 if failures else 0)
