#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Inside cir-env, from the tree root: lazy emission against eager emission.

usage: box_lazy_check.py OPT JOBS STREAM...   (OPT: -O0, -O2, ...)

Every stream is compiled with `edg-cir OPT --emit=asm --stats --lazy-symbols`
(lazy, the default) and with --eager-bodies.  Per stream: the exit status
(lazy may succeed where eager fails with an emitter-only diagnostic: counted
as `eager-only`, never the other way round), the diagnostics (lazy a subset of
eager), and the assembly symbol by symbol (the same functions, the same body
with local labels renumbered per function, the same remaining lines as a
multiset).  Prints the counts and every stream that is not `same`; also the
sums of the deferred, replayed and dropped counts.  Exit 1 if any stream is
not same/eager-only.
"""
import collections, concurrent.futures, os, re, subprocess, sys, tempfile
from pathlib import Path

EDG = "build/cir/bin/edg-cir"
LABEL = re.compile(r"\.L(BB|tmp|func_begin|func_end|cfi|CPI|set|exception|"
                   r"eh_func_end|cst_begin|cst_end|ttbase|ttbaseref|"
                   r"callsite_end|JTI)(\d+)(_\d+)?")
TYPE = re.compile(r"^\t\.type\t([^,\s]+),@function")
COUNTS = re.compile(r"stats: lazy bodies: (\d+) deferred, (\d+) replayed, "
                    r"(\d+) dropped")


KEPT = re.compile(r"(\d+) dropped bodies named by")


def renumber(lines):
    seen = {}
    def sub(m):
        key = (m.group(1), m.group(2))
        return ".L%s%d%s" % (m.group(1), seen.setdefault(key, len(seen)),
                             m.group(3) or "")
    return [LABEL.sub(sub, l) for l in lines]


def split(text):
    funcs, rest, name, cur = {}, [], None, []
    for line in text.split("\n"):
        m = TYPE.match(line)
        if m and name is None:
            name, cur = m.group(1), [line]
        elif name is not None:
            cur.append(line)
            if line.startswith("# -- End function"):
                funcs[name] = renumber(cur)
                name = None
        elif line.strip() and not line.startswith("#"):
            rest.append(LABEL.sub(".LX", line))
    return funcs, collections.Counter(rest)


def run(stream, opt, extra, out):
    p = subprocess.run([EDG, opt, "--emit=asm", "--stats", "-o", out, stream]
                       + extra + (["--keep-going"] if os.environ.get("KEEP") else []),
                       capture_output=True, text=True, timeout=300)
    diags = [l for l in p.stderr.splitlines() if "stats:" not in l]
    return p.returncode, diags, p.stderr


def work(job):
    stream, opt = job
    d = tempfile.mkdtemp(prefix="lz")
    rl, dl, el = run(stream, opt, ["--lazy-symbols"], d + "/l.s")
    re_, de, ee = run(stream, opt, ["--eager-bodies"], d + "/e.s")
    m = COUNTS.search(el)
    counts = tuple(int(x) for x in m.groups()) if m else (-1, -1, -1)
    k = KEPT.search(el)
    counts = counts + (int(k.group(1)) if k else 0,)
    if rl != re_:
        verdict = "eager-only" if (rl == 0 and re_ != 0) else "STATUS lazy=%d eager=%d" % (rl, re_)
    elif any(x not in de for x in dl):
        verdict = "DIAG lazy has " + [x for x in dl if x not in de][0][:100]
    elif rl != 0:
        verdict = "same-failure"
    else:
        fl, ml = split(open(d + "/l.s").read())
        fe, me = split(open(d + "/e.s").read())
        if set(fl) != set(fe):
            verdict = "FUNCS lazy-only=%s eager-only=%s" % (sorted(set(fl) - set(fe))[:2], sorted(set(fe) - set(fl))[:2])
        elif any(fl[k] != fe[k] for k in fl):
            verdict = "BODY " + [k for k in fl if fl[k] != fe[k]][0]
        elif ml != me:
            verdict = "REST " + str(list((ml - me) + (me - ml))[:2])[:120]
        else:
            verdict = "same" if open(d + "/l.s").read() == open(d + "/e.s").read() else "same-reordered"
    subprocess.run(["rm", "-rf", d])
    return stream, verdict, counts


if __name__ == "__main__":
    opt, jobs, streams = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
    c = collections.Counter()
    tot = [0, 0, 0, 0]
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for stream, verdict, counts in ex.map(work, [(s, opt) for s in streams]):
            c[verdict.split()[0]] += 1
            if counts[0] > 0:
                tot = [a + b for a, b in zip(tot, counts)]
            if verdict.split()[0] not in ("same", "same-reordered", "same-failure", "eager-only"):
                bad += 1
                print(os.path.basename(stream), verdict)
            elif verdict == "eager-only":
                print("INFO eager-only", os.path.basename(stream))
    print(dict(c), "deferred/replayed/dropped/named-by-dead-entities:", tot)
    sys.exit(1 if bad else 0)
