#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env: run two edg-cir binaries over every .ecil stream of a
directory and report the streams whose result differs (exit status or first
diagnostic line).  Each binary runs with --emit=cir (the emitter, finish,
verify and the CIR passes; no lowering) unless --emit says otherwise.

usage: box_stream_check.py DIR OLD_EDG_CIR NEW_EDG_CIR [--emit X] [-j N]
       [--timeout S] [--keep-going]
Prints the counts and "NEWLY-REJECTED stream: message" / "NEWLY-ACCEPTED" /
"CHANGED" lines; exit status 1 when a stream is newly rejected or changed.
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys
from pathlib import Path


def run(binary, stream, emit, timeout, keep_going):
    lib = str(Path(binary).resolve().parent.parent / "lib")
    env = dict(os.environ, LD_LIBRARY_PATH=lib)
    cmd = [binary, f"--emit={emit}", "-o", "/dev/null"]
    if keep_going:
        cmd.append("--keep-going")
    try:
        p = subprocess.run(cmd + [stream], env=env, capture_output=True,
                           text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    first = ""
    for line in p.stderr.splitlines():
        if "error" in line or "internal" in line or "crash" in line:
            first = line.strip()[:240]
            break
    return p.returncode, first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--emit", default="cir")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--keep-going", action="store_true")
    args = ap.parse_args()
    streams = sorted(str(p) for p in Path(args.dir).glob("*.ecil"))
    if not streams:
        print("no streams")
        return 2

    def both(s):
        return (s, run(args.old, s, args.emit, args.timeout, args.keep_going),
                run(args.new, s, args.emit, args.timeout, args.keep_going))

    counts = {"same": 0, "ok": 0, "fail": 0}
    bad = []
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        for s, a, b in pool.map(both, streams):
            name = Path(s).name
            counts["ok" if b[0] == 0 else "fail"] += 1
            if a == b:
                counts["same"] += 1
            elif a[0] == 0 and b[0] != 0:
                bad.append(f"NEWLY-REJECTED {name}: {b[1]}")
            elif a[0] != 0 and b[0] == 0:
                bad.append(f"NEWLY-ACCEPTED {name}: was {a[1]}")
            elif a[0] == b[0] and a[1] != b[1]:
                bad.append(f"CHANGED {name}: {a[1]!r} -> {b[1]!r}")
            else:
                bad.append(f"CHANGED {name}: {a} -> {b}")
    print(f"{len(streams)} streams: {counts['same']} identical results "
          f"(new binary: {counts['ok']} ok, {counts['fail']} rejected)")
    for line in bad:
        print(line)
    return 1 if any(l.startswith(("NEWLY-REJECTED", "CHANGED")) for l in bad) else 0


sys.exit(main())
