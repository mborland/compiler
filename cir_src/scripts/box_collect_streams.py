#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env, from the tree root: keep the ECIL stream of every test case
of a corpus filter list.

Each case is compiled like edg-cir-compare-modes does (eccp -S with the
standard options and the test's own), with the front end replaced by
cir_src/scripts/keep-stream-fe.sh, which runs cpfe-cir with --ecil_output into
the output directory instead of invoking the library.

usage: box_collect_streams.py OUTDIR @FILTERFILE [--cxx] [--types rp,lp] [-j N]
Writes OUTDIR/<test path, '/' as '__'>.<case>[.<n>].ecil and OUTDIR/index.tsv
(stream path, test path, case number, front end status).
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path.cwd()
sys.path.insert(0, str(root / "dev_tools" / "pylibs"))
import edgtest                      # noqa: E402
import edgtest.runtest as runtest   # noqa: E402

STANDARD = ["-DTEST_NUMBER=1", "--gnu_version=999999", "--no_wrap",
            "--no_wrap_diagnostics", "-e", "200", "--gcc"]
STANDARD_CXX = ["-DTEST_NUMBER=1", "--gnu_version=999999", "-tused", "-e",
                "200", "--no_wrap", "--no_wrap_diagnostics"]


def exports(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        m = re.match(r'export (\w+)="?([^"]*)"?$', line)
        if m:
            result[m.group(1)] = m.group(2)
    return result


def read_filters(spec):
    lines = [l.strip() for l in Path(spec[1:]).read_text().splitlines()]
    lines = [l for l in lines if l and not l.startswith("#")]
    return ([l for l in lines if not l.startswith("!")],
            [l[1:] for l in lines if l.startswith("!")])


def find_tests(tests_dir, positive, negative):
    found = []
    for suite in sorted(p for p in tests_dir.iterdir()
                        if p.is_dir() and not p.name.startswith(".")):
        for path in sorted(edgtest.walk_test_directory(
                suite, edgtest.DEFAULT_TEST_EXTENSIONS)):
            rel = f"{suite.name}/{path.relative_to(suite).as_posix()}"
            if positive and not any(rel.startswith(p) for p in positive):
                continue
            if any(rel.startswith(n) for n in negative):
                continue
            found.append(path)
    return found


def one(test, tests_dir, env, out, standard, index):
    desc = runtest.load_description(
        test, top_dir=tests_dir / test.relative_to(tests_dir).parts[0])
    rel = test.relative_to(tests_dir).as_posix().replace("/", "__")
    rows = []
    for case, (mode, base, extra) in enumerate(desc.traverse_cases()):
        options = [*standard, *base, *extra]
        work = Path(tempfile.mkdtemp(prefix="collect."))
        try:
            shutil.copy2(test, work / test.name)
            for h in test.parent.glob("*.h"):
                shutil.copy2(h, work / h.name)
            e = dict(env)
            e["KEEP_STREAM"] = str(out / f"{rel}.{case}")
            try:
                subprocess.run([e["ECCP"], "-S", *options, test.name],
                               cwd=work, env=e, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=300)
            except subprocess.TimeoutExpired:
                pass
        finally:
            shutil.rmtree(work, ignore_errors=True)
        found = sorted(out.glob(f"{rel}.{case}*.ecil"))
        for s in found:
            rows.append((s.name, rel, case))
        if not found:
            rows.append(("-", rel, case))   # a case without a stream
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("filters")
    ap.add_argument("--cxx", action="store_true")
    ap.add_argument("--types", default="")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args()
    out = Path(args.outdir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    tests_dir = root / "tests" / "tests"
    env = dict(os.environ)
    env.update(exports(root / "build/gcc-cir/environment.sh"))
    env["EDG_BASE"] = str(root / "bases/docker/cir-env/gcc")
    env["ECCP_LIBDIR"] = env.get("EDG_CIR_RT_LIBDIR", "")
    env["ECCP"] = str(root / "util/eccp.sh")
    env["KEEP_FE"] = env["CPFE_CIR"]
    env["CPFE"] = str(root / "cir_src/scripts/keep-stream-fe.sh")
    pos, neg = read_filters(args.filters)
    tests = find_tests(tests_dir, pos, neg)
    if args.types:
        wanted = set(args.types.split(","))
        tests = [t for t in tests if any(
            runtest.test_type_to_string(m) in wanted
            for m, _, _ in runtest.load_description(
                t, top_dir=tests_dir / t.relative_to(tests_dir).parts[0]
            ).traverse_cases())]
    standard = STANDARD_CXX if args.cxx else STANDARD
    print(f"collecting {len(tests)} tests", flush=True)
    rows = []
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        for r in pool.map(lambda t: one(t, tests_dir, env, out, standard, 0),
                          tests):
            rows.extend(r)
    (out / "index.tsv").write_text(
        "".join(f"{a}\t{b}\t{c}\n" for a, b, c in sorted(rows)))
    cases = {(b, c) for _, b, c in rows}
    streams = sum(1 for a, _, _ in rows if a != "-")
    print(f"{len(cases)} cases of {len(tests)} tests, {streams} streams, "
          f"{len(cases) - len({(b, c) for a, b, c in rows if a != '-'})} "
          f"cases without a stream, in {out}")


main()
