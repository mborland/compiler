#!/usr/bin/env python3
"""Write the ECIL stream of every test case of a filter list and record its
hash: "name<TAB>exit status<TAB>sha256<TAB>bytes" per case, in a TSV.

Two runs with different front ends over the same corpus show whether a change
of the writer changes any stream (diff the two TSVs).  Run it in the cir-env
container from the repository root, with PYTHONPATH=dev_tools/pylibs:

  stream_sweep.py @tests/.edgy/filters/cir-cpp-corpus.txt --types rp,lp --cxx \
      --fe build/gcc-cir/bin/cpfe-cir --out .tmp/streams-a -j 12

--keep also keeps the streams, as <out>/<name>.<case>.ecil.  --grep TEXT
also writes <out>/matches.txt, the cases whose output contains TEXT with the
number of occurrences, and the total (whitespace is normalized).  --count-ir runs the front end
under cachegrind and adds the instruction count to each row (the sum over a
corpus measures the writer; use the release build); --sample N takes every Nth
test.  The options are those that
edgy gives the tests of the GNU testsuite (edg-cir-compare-modes).
"""

import argparse
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

from pathlib import Path
from typing import Dict, List, Tuple

import edgtest
import edgtest.runtest as runtest

STANDARD_OPTIONS = [
  '-DTEST_NUMBER=1', '--gnu_version=999999', '-e', '200', '--no_wrap',
  '--no_wrap_diagnostics', '--gcc'
]
STANDARD_CXX_OPTIONS = [
  '-DTEST_NUMBER=1', '--gnu_version=999999', '-tused', '-e', '200',
  '--no_wrap', '--no_wrap_diagnostics'
]


def read_filters(spec: str) -> Tuple[List[str], List[str]]:
  lines: List[str] = []
  if spec.startswith('@'):
    for line in Path(spec[1:]).read_text().splitlines():
      line = line.strip()
      if line and not line.startswith('#'):
        lines.append(line)
  else:
    lines = spec.split(',')
  return ([l for l in lines if not l.startswith('!')],
          [l[1:] for l in lines if l.startswith('!')])


def find_tests(tests_dir: Path, positive: List[str],
               negative: List[str]) -> List[Path]:
  found = []
  for suite in sorted(p for p in tests_dir.iterdir()
                      if p.is_dir() and not p.name.startswith('.')):
    for path in sorted(edgtest.walk_test_directory(
        suite, edgtest.DEFAULT_TEST_EXTENSIONS)):
      rel = f"{suite.name}/{path.relative_to(suite).as_posix()}"
      if positive and not any(rel.startswith(p) for p in positive):
        continue
      if any(rel.startswith(n) for n in negative):
        continue
      found.append(path)
  return found


def sweep_one(test: Path, tests_dir: Path, env: Dict[str, str],
              standard: List[str], out: Path, keep: bool, grep: str
              ) -> List[Tuple[str, int, str, int, int, int]]:
  description = runtest.load_description(
    test, top_dir = tests_dir / test.relative_to(tests_dir).parts[0])
  rel = test.relative_to(tests_dir).as_posix().replace('/', '__')
  rows = []
  for case_number, (mode, base_options, case_options) in enumerate(
      description.traverse_cases()):
    options = [*standard, *base_options, *case_options]
    work = Path(tempfile.mkdtemp(prefix = 'streams.'))
    try:
      local = work / test.name
      shutil.copy2(test, local)
      for extra in test.parent.glob('*.h'):
        shutil.copy2(extra, work / extra.name)
      stream = work / 'out.ecil'
      run_env = dict(env)
      run_env['ECIL_OUT'] = str(stream)
      try:
        proc = subprocess.run(
          [env['ECCP'], '--cpfe_only', *options, local.name], cwd = work,
          env = run_env, stdout = subprocess.PIPE,
          stderr = subprocess.STDOUT, timeout = 300)
        rc = proc.returncode
        text = ' '.join(proc.stdout.decode(errors = 'replace').split())
      except subprocess.TimeoutExpired:
        rc = 124
        text = ''
      matched = text.count(grep) if grep else 0
      data = stream.read_bytes() if stream.exists() else b''
      name = f"{rel}.{case_number}"
      ir = 0
      cg = work / 'out.ecil.cg'
      if cg.exists():
        for line in cg.read_text().splitlines():
          if line.startswith('summary:'):
            ir = int(line.split()[1])
      rows.append((name, rc, hashlib.sha256(data).hexdigest(), len(data),
                   matched, ir))
      if keep and data:
        (out / f"{name}.ecil").write_bytes(data)
    finally:
      shutil.rmtree(work, ignore_errors = True)
  return rows


def main() -> None:
  parser = argparse.ArgumentParser(description = __doc__.split('\n')[0])
  parser.add_argument('filters')
  parser.add_argument('--types', default = '')
  parser.add_argument('--cxx', action = 'store_true')
  parser.add_argument('--fe', required = True, help = 'the cpfe-cir to run')
  parser.add_argument('--root', default = '.')
  parser.add_argument('--out', required = True)
  parser.add_argument('--keep', action = 'store_true')
  parser.add_argument('--grep', default = '')
  parser.add_argument('--count-ir', action = 'store_true')
  parser.add_argument('--sample', type = int, default = 1)
  parser.add_argument('-j', dest = 'jobs', type = int,
                      default = os.cpu_count() or 1)
  args = parser.parse_args()

  root = Path(args.root).resolve()
  tests_dir = root / 'tests' / 'tests'
  out = Path(args.out).resolve()
  out.mkdir(parents = True, exist_ok = True)
  shim = out / 'shim.sh'
  prefix = ('valgrind --tool=cachegrind --cache-sim=no -q '
            '--cachegrind-out-file=$ECIL_OUT.cg ' if args.count_ir else '')
  shim.write_text(f"#!/bin/bash\nexec timeout 1200 {prefix}"
                  f"{Path(args.fe).resolve()} --ecil_output=$ECIL_OUT \"$@\"\n")
  shim.chmod(0o755)
  env = dict(os.environ)
  env['EDG_BASE'] = str(root / 'bases/docker/cir-env/gcc')
  env['ECCP'] = str(root / 'util/eccp.sh')
  env['CPFE'] = str(shim)
  env['CPFE_CP'] = str(shim)
  env['ECCP_LIBDIR'] = str(root / 'build/cir/lib')
  env['EDG_CIR_RT_LIBDIR'] = str(root / 'build/cir/lib')

  positive, negative = read_filters(args.filters)
  tests = find_tests(tests_dir, positive, negative)
  if args.types:
    wanted = set(args.types.split(','))
    tests = [t for t in tests if any(
      runtest.test_type_to_string(m) in wanted
      for m, _, _ in runtest.load_description(
        t, top_dir = tests_dir / t.relative_to(tests_dir).parts[0]
      ).traverse_cases())]
  tests = tests[::max(args.sample, 1)]
  standard = STANDARD_CXX_OPTIONS if args.cxx else STANDARD_OPTIONS
  print(f"{len(tests)} tests, {args.jobs} jobs", flush = True)
  rows: List[Tuple[str, int, str, int, int, int]] = []
  with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
    futures = [pool.submit(sweep_one, t, tests_dir, env, standard, out,
                           args.keep, args.grep) for t in tests]
    for future in concurrent.futures.as_completed(futures):
      rows.extend(future.result())
  rows.sort()
  with open(out / 'streams.tsv', 'w') as tsv:
    for name, rc, digest, size, matched, ir in rows:
      extra = f"\t{ir}" if args.count_ir else ''
      tsv.write(f"{name}\t{rc}\t{digest}\t{size}{extra}\n")
  if args.grep:
    (out / 'matches.txt').write_text(''.join(
      f"{r[0]}\t{r[4]}\n" for r in rows if r[4]) +
      f"total\t{sum(r[4] for r in rows)}\n")
  print(f"{len(rows)} cases, {sum(1 for r in rows if r[3] > 0)} with a "
        f"stream, written to {out / 'streams.tsv'}")
  if args.count_ir:
    print(f"total Ir {sum(r[5] for r in rows)}")


main()
