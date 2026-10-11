#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Diff the CIR edg-cir emits for an ECIL stream against clang's CIRGen.

usage: cir-compare.py --ecil FILE.ecil --c FILE.c [--func NAME]...
       [--clang PATH] [--edg-cir PATH] [--triple T] [--std -std=c11]

Both sides are normalized: SSA values are renumbered in order of definition,
locations, module attributes and symbol visibility noise are dropped, and
only functions and globals present on both sides are compared.  The exit
status is 1 when a compared function differs.
"""
import argparse, difflib, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--ecil", required=True)
ap.add_argument("--c", required=True)
ap.add_argument("--func", action="append", default=[])
ap.add_argument("--clang", default="/edg/llvm-cir/bin/clang")
ap.add_argument("--edg-cir", default=os.environ.get(
    "EDG_CIR", "build/cir/bin/edg-cir"))
ap.add_argument("--triple", default="x86_64-unknown-linux-gnu")
ap.add_argument("--std", default="-std=c11")
ap.add_argument("--all", action="store_true",
                help="show the normalized text of equal functions too")
ap.add_argument("--cxx", action="store_true",
                help="C++ mode: --c is a C++ file (-std=c++17; cc1 has exceptions off by default) "
                "and the C++ normalizations below apply")
ap.add_argument("--cxx-std", default="c++17",
                help="with --cxx, the -std= value of the clang side")
ap.add_argument("--loose", action="store_true",
                help="with --cxx, also ignore alignment(N) function "
                "attributes, align(N) of loads, stores and allocas, the "
                "init marker of allocas, packed and the explicit pad arrays "
                "of record types (they are not compared by default)")
ap.add_argument("--fe", action="store_true",
                help="front-end output mode (with --cxx): also fold the noise "
                "that comes from how the writer orders and wraps things, "
                "documented in doc/research/cxx_cir_differential.md: init/"
                "align/deref attributes, the __retval round trip of scalar "
                "returns, the body-wide cir.scope + trailing unreachable, "
                "the order and names of allocas (temporaries are one name), "
                "inline_hint/alignment/no_inline function attributes")
ap.add_argument("--keep-going", action="store_true",
                help="run edg-cir with --keep-going (dropped definitions are warnings)")
ap.add_argument("--clang-arg", action="append", default=[],
                help="extra clang -cc1 argument (e.g. -internal-isystem DIR)")
ap.add_argument("--structors", action="store_true",
                help="treat the constructor/destructor variants C1/C2/C5 and "
                "D0/D1/D2/D5 of a symbol as one (the library emits the base "
                "variant plus an alias or a use replacement, clang chooses "
                "per linkage)")
ap.add_argument("--passes", action="store_true",
                help="run the CIR passes on both sides (default: neither)")
ap.add_argument("--global-order", action="store_true",
                help="also compare the order of the globals that have a ctor "
                "region (dynamic initialization order = module order)")
args = ap.parse_args()
if args.cxx:
    args.structors = True


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout, r.stderr, r.returncode


def normalize_cxx(text):
    """C++ noise that is not part of the comparison: per-argument LLVM
    attribute dictionaries, function attribute lists, comdat/func_info
    decorations and the record type alias spelling.  alignment(N), packed
    and explicit pad arrays are compared unless --loose is given (the
    lowering inserts equal padding itself, so they are cosmetic)."""
    text = re.sub(r"\s*\{llvm\.[^{}]*\}", "", text)
    text = re.sub(r"-> \((![^(),]+)\)", r"-> \1", text)
    text = re.sub(r" attributes \{[^{}]*\}", "", text)
    text = re.sub(r"\bcomdat(\(\"[^\"]*\"\))?\s*", "", text)
    text = re.sub(r"\bfunc_info<[^\n]*?>>?\s*(?=\{|$)", "", text, flags=re.M)
    text = re.sub(r"\bno_inline\s*|\bnothrow\b\s*", "", text)
    text = re.sub(r" ast\(#cir\.var\.decl\.ast\)", "", text)
    text = re.sub(r"!rec_(\w+?)2Ebase", r"!rec_\1.base", text)
    if args.loose:
        text = re.sub(r"\balign\(\d+\)\s*", "", text)
        text = re.sub(r"(cir\.alloca \"[^\"]*\") init\b", r"\1", text)
        text = re.sub(r"\balignment\(\d+\)\s*", "", text)
        text = re.sub(r", pad !cir\.array<!u8i x \d+>", "", text)
        text = re.sub(r"\bpacked ", "", text)
    return text


def structor_symbol(sym):
    """Rewrite the constructor/destructor variant of an Itanium nested name
    (_ZN ... C1E / D2Ev ...) to Cx / Dx.  Only a C[1-5] or D[0-5] that is a
    component of the nested name counts: a source name such as N::C1 is
    length-prefixed (2C1) and is skipped, template arguments are skipped."""
    if not sym.startswith("_ZN"):
        return sym
    out = list(sym)
    i = 3
    depth = 0          # open I/N/L/X ... E groups inside the nested name
    n = len(sym)
    while i < n:
        c = sym[i]
        if c.isdigit():                      # <length><identifier>
            j = i
            while j < n and sym[j].isdigit():
                j += 1
            i = j + int(sym[i:j])
            continue
        if c in "INLXJ":
            depth += 1
        elif c == "E":
            if depth == 0:
                break                        # end of the nested name
            depth -= 1
        elif depth == 0 and c in "CD" and i + 1 < n and \
                sym[i + 1] in ("12345" if c == "C" else "012345"):
            out[i + 1] = "x"
            i += 2
            continue
        i += 1
    return "".join(out)


def normalize_structors(text):
    return re.sub(r"@_Z[\w.$]+", lambda m: "@" + structor_symbol(m.group(0)[1:]),
                  text)


def name_strings(text):
    """Name each private string literal by its contents: both sides number
    them (.str, .str.1, ...) by their own emission order."""
    names = {}
    for m in re.finditer(r'cir\.global [^\n@]*@"(\.str(?:\.\d+)?)" = '
                         r'#cir\.const_array<"((?:[^"\\]|\\.)*)"', text):
        key = re.sub(r"\W", "_", m.group(2))[:40]
        names[m.group(1)] = ".str." + key
    for old, new in names.items():
        text = text.replace('@"%s"' % old, '@"%s"' % new)
    return text


def normalize(text):
    if args.fe:
        text = name_strings(text)
    if args.cxx:
        text = normalize_cxx(text)
    if args.structors:
        text = normalize_structors(text)
    text = re.sub(r"\s+loc\([^\n]*?\)(?=\s*(?:\{|$))", "", text, flags=re.M)
    text = re.sub(r" loc\(#loc\d+\)", "", text)
    text = re.sub(r" loc\(fused\[[^\]]*\]\)", "", text)
    text = re.sub(r' loc\("[^"]*":\d+:\d+\)', "", text)
    text = re.sub(r"^#loc.*$\n?", "", text, flags=re.M)
    text = re.sub(r"\bdso_local\b\s*", "", text)
    text = re.sub(r" +", " ", text)
    return text


TEMP_NAME = re.compile(r'"(?:agg\.tmp\.ensured|agg\.tmp|ref\.tmp|tmp|'
                       r'exn\.slot|cleanup\.dest\.slot|ref\.tmp\.exprcleanup'
                       r')\d*"')
FE_NOISE = [(re.compile(r" init(?= :)"), ""), (re.compile(r" init\b"), ""),
            (re.compile(r"\balign\(\d+\)\s*"), ""),
            (re.compile(r"\bderef\s+"), ""),
            (re.compile(r"(cir\.alloca \"[^\"]*\") const\b"), r"\1"),
            (re.compile(r"\b(?:inline_hint|no_inline|always_inline|"
                        r"alignment\(\d+\))\s*"), ""),
            (re.compile(r"\bcir\.cast bitcast"), "cir.cast bitcast")]
NONNULL = {"clang": 0, "edg-cir": 0}
NULLCHK = {}   # (side, function) -> base/derived casts that keep null


def null_checked_casts(lines):
    """Base/derived casts at a non-zero offset that preserve null (no
    nonnull): the ones that need a check."""
    return sum(1 for l in lines
               if re.search(r"cir\.(?:base|derived)_class_addr %\S+ \[[1-9]",
                            l) and " nonnull " not in l)


def fe_canon_block(lines, side):
    """Fold writer-order noise in one function (lines are stripped)."""
    NONNULL[side] += sum(1 for l in lines if "_class_addr nonnull" in l)
    out = []
    for l in lines:
        for rx, rep in FE_NOISE:
            l = rx.sub(rep, l)
        l = l.replace("nonnull ", "")
        out.append(l)
    lines = out
    # __retval round trip: store V,R; L = load R; return L  ->  return V
    text = "\n".join(lines)
    text = re.sub(r"cir\.store (%\S+), (%\S+) : [^\n]*\n(%\S+) = cir\.load "
                  r"\2 : [^\n]*\ncir\.return \3 : ", r"cir.return \1 : ", text)
    # an alloca named __retval that nothing uses any more
    for m in re.finditer(r"^(%\S+) = cir\.alloca \"__retval\"", text, re.M):
        if len(re.findall(re.escape(m.group(1)) + r"(?![\w.])", text)) == 1:
            text = re.sub(r"^" + re.escape(m.group(0)) + r"[^\n]*\n", "",
                          text, flags=re.M)
    lines = text.split("\n")
    # the body-wide cir.scope with a trailing cir.unreachable (writer shape)
    for k, l in enumerate(lines):
        if l.startswith("cir.scope {"):
            depth = 0
            for j in range(k, len(lines)):
                depth += lines[j].count("{") - lines[j].count("}")
                if depth == 0:
                    if j + 2 < len(lines) + 1 and lines[j + 1:j + 3] == \
                            ["cir.unreachable", "}"]:
                        lines = lines[:k] + lines[k + 1:j] + lines[j + 2:]
                    break
            break
    # clang loads `this` once and reuses it; every use of it is one value
    text = "\n".join(lines)
    for m in re.finditer(r"^(%\S+) = cir\.load (%@this|%\S+) : ", text, re.M):
        pass
    this_slots = re.findall(r"^(%\S+) = cir\.alloca \"this\"", text, re.M)
    for slot in this_slots:
        text = re.sub(r"^(%\S+) = cir\.load " + re.escape(slot) +
                      r" : [^\n]*\n", lambda mm: "", text, flags=re.M) \
            if False else text
        loads = re.findall(r"^(%\S+) = cir\.load " + re.escape(slot) +
                           r" : [^\n]*$", text, re.M)
        text = re.sub(r"^%\S+ = cir\.load " + re.escape(slot) +
                      r" : [^\n]*\n", "", text, flags=re.M)
        for v in loads:
            text = re.sub(re.escape(v) + r"(?![\w.])", "%THIS", text)
    # a chain of base casts equals one cast with the summed offset
    ls = text.split("\n")
    cast = re.compile(r"^(%\S+) = cir\.base_class_addr (%\S+) \[(\d+)\] : "
                      r"(.*) -> (.*)$")
    k = 0
    while k + 1 < len(ls):
        m1, m2 = cast.match(ls[k]), cast.match(ls[k + 1])
        if m1 and m2 and m2.group(2) == m1.group(1) and \
                len(re.findall(re.escape(m1.group(1)) + r"(?![\w.])",
                               text)) == 2:
            ls[k:k + 2] = ["%s = cir.base_class_addr %s [%d] : %s -> %s" %
                           (m2.group(1), m1.group(2),
                            int(m1.group(3)) + int(m2.group(3)),
                            m1.group(4), m2.group(5))]
            text = "\n".join(ls)
            k = max(k - 1, 0)
            continue
        k += 1
    text = "\n".join(ls)
    lines = text.split("\n")
    # allocas: name by their alloca name, sorted; temporaries are one name.
    # MLIR reuses SSA numbers in sibling regions and clang puts the allocas of
    # scoped locals inside those regions, so names are resolved with a scope
    # stack (innermost first), not by number alone.
    allocas = []
    scopes = [{}]
    rest = []
    counts = {}
    def_re = re.compile(r"^(%\S+) = cir\.alloca (\"[^\"]*\")( : .*)$")
    use_re = re.compile(r"%\d+(?![\w.@])")
    for l in lines[1:]:
        m = def_re.match(l)
        rhs = l
        if not m:
            def sub(mm):
                for sc in reversed(scopes):
                    if mm.group(0) in sc:
                        return sc[mm.group(0)]
                return mm.group(0)
            head_part, sep, tail = l.partition(" = ")
            if sep and head_part.startswith("%"):
                rhs = head_part + sep + use_re.sub(sub, tail)
            else:
                rhs = use_re.sub(sub, l)
        if m:
            nm = TEMP_NAME.sub('"TMP"', m.group(2))
            n = counts.get(nm, 0)
            counts[nm] = n + 1
            key = nm.strip('"') + ("#%d" % n if n else "")
            scopes[-1][m.group(1)] = "%@" + key
            allocas.append((nm, m.group(3)))
        else:
            rest.append(rhs)
        for _ in range(l.count("}")):
            if len(scopes) > 1:
                scopes.pop()
        for _ in range(l.count("{")):
            scopes.append({})
    allocas.sort()
    head = [lines[0]] if lines else []
    def short(m):
        n = m.group(0)[5:].replace("28anonymous_namespace293A3A", "")
        n = re.split(r"3C", n)[0]
        return "!rec_" + re.sub(r"(?:2E\d+)+$", "", n)
    out = head + ["cir.alloca %s%s" % a for a in allocas] + rest
    return [re.sub(r"!rec_\w+", short, l) for l in out]


def op_signatures(block):
    """The multiset of operation shapes of a function (SSA numbers dropped)."""
    from collections import Counter
    sigs = Counter()
    for l in block.split("\n"):
        if l.endswith("{") and not l.startswith("cir.func"):
            l = l.rstrip(" {")
        if l in ("}", "cir.yield", "cir.unreachable") or l.startswith("cir.func"):
            continue
        l = re.sub(r"^%\d+ = ", "", l)
        l = re.sub(r"%\d+", "%", l)
        sigs[l] += 1
    return sigs


def call_sequence(block):
    """The ordered effect events of a function: callee names, copies, exit
    destructor registrations, deletes (the semantic skeleton --fe compares
    after the multiset)."""
    seq = []
    for l in block.split("\n"):
        m = re.search(r"cir\.call (@[\w.$]+)\(", l)
        if m:
            seq.append(m.group(1))
            continue
        if re.search(r"cir\.call %|cir\.call_indirect", l):
            seq.append("indirect")
        elif "cir.libc.memcpy" in l or "cir.copy " in l:
            seq.append("copy")
        elif "cir.libc.memset" in l:
            seq.append("memset")
        elif "cir.register_exit_dtor" in l or "cir.delete_array" in l \
                or "cir.array.ctor" in l or "cir.array.dtor" in l \
                or "cir.local_init" in l:
            seq.append(l.split(" ")[0] if not l.startswith("%") else
                       l.split(" ")[2])
    return seq


def split_items(text, side="edg-cir"):
    """-> {('func'|'global', name): normalized block text}"""
    items = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"\s*cir\.(func|global)\b.*?@\"?([\w.$]+)\"?", lines[i])
        if not m:
            i += 1
            continue
        kind, name = m.group(1), m.group(2)
        depth = lines[i].count("{") - lines[i].count("}")
        block = [lines[i]]
        i += 1
        while depth > 0 and i < len(lines):
            depth += lines[i].count("{") - lines[i].count("}")
            block.append(lines[i])
            i += 1
        if args.fe and kind == "global":
            blk = []
            for l in block:
                for rx, rep in FE_NOISE:
                    l = rx.sub(rep, l)
                blk.append(l)
            block = blk
        if args.fe and kind == "func":
            blk = [l.strip() for l in block if l.strip()]
            NULLCHK[(side, name)] = null_checked_casts(blk)
            block = fe_canon_block(blk, side)
        text_ = renumber("\n".join(block))
        # With --structors the variants of a constructor share one key; the
        # real body wins over an alias or a forwarding C1.
        if (kind, name) not in items or \
                len(text_) > len(items[(kind, name)]):
            items[(kind, name)] = text_
    return items


def renumber(block):
    names = {}

    def sub(m):
        key = m.group(0)
        if key not in names:
            names[key] = "%%%d" % len(names)
        return names[key]
    block = re.sub(r"%[A-Za-z_][\w.]*|%\d+", sub, block)
    block = re.sub(r"\^bb\d+", "^bb", block)
    return "\n".join(l.strip() for l in block.split("\n") if l.strip())


ours, err1, rc1 = run([args.edg_cir,
                       "--emit=cir-lowered" if args.passes else "--emit=cir"]
                      + ([] if args.passes else ["--disable-cir-passes"])
                      + (["--keep-going"] if args.keep_going else [])
                      + [args.ecil])
if rc1:
    sys.exit("edg-cir failed:\n" + err1)
lang = (["-x", "c++", "-std=" + args.cxx_std] if args.cxx
        else [])
theirs, err2, rc2 = run([args.clang, "-cc1", "-triple", args.triple]
                        + lang + args.clang_arg + ["-fclangir", "-emit-cir"]
                      + ([] if args.passes else ["-clangir-disable-passes"])
                      + [args.c, "-o", "-"])
if rc2:
    sys.exit("clang failed:\n" + err2)
a = split_items(normalize(ours), "edg-cir")
b = split_items(normalize(theirs), "clang")
status = 0
for key in sorted(set(a) & set(b)):
    if args.func and key[1] not in args.func:
        continue
    if a[key] == b[key]:
        print("same  %s @%s" % key)
        if args.all:
            print(a[key])
    else:
        status = 1
        print("DIFF  %s @%s" % key)
        if args.fe and key[0] == "func":
            ca, cb = op_signatures(a[key]), op_signatures(b[key])
            only_b = cb - ca
            only_a = ca - cb
            if not only_a and not only_b:
                print("  ops: same multiset (order or SSA naming only)")
            else:
                for l, n in sorted(only_b.items()):
                    print("  ops: clang only x%d: %s" % (n, l[:150]))
                for l, n in sorted(only_a.items()):
                    print("  ops: edg-cir only x%d: %s" % (n, l[:150]))
            na, nb = NULLCHK.get(("edg-cir", key[1]), 0), \
                NULLCHK.get(("clang", key[1]), 0)
            if na != nb:
                print("  casts: null-checked clang=%d edg-cir=%d" % (nb, na))
            sa, sb = call_sequence(a[key]), call_sequence(b[key])
            if sa == sb:
                print("  calls: same sequence (%d events)" % len(sa))
            else:
                print("  calls: DIFFER clang=%s edg-cir=%s" % (sb, sa))
        for line in difflib.unified_diff(
                b[key].split("\n"), a[key].split("\n"), "clang", "edg-cir",
                lineterm="", n=2):
            print("  " + line)
if args.fe:
    def rec_types(text):
        d = {}
        for m in re.finditer(r"^!(rec_\w+) = (!cir\.[^\n]*)$", text, re.M):
            d[m.group(1)] = m.group(2)
        return d
    ta, tb = rec_types(ours), rec_types(theirs)
    ndiff = 0
    for n in sorted(set(ta) & set(tb)):
        if ta[n] != tb[n]:
            ndiff += 1
            status = 1
            print("DIFF  type !%s" % n)
            print("  clang:   %s" % tb[n][:300])
            print("  edg-cir: %s" % ta[n][:300])
    print("record types: %d common, %d differ, %d only edg-cir, %d only clang"
          % (len(set(ta) & set(tb)), ndiff, len(set(ta) - set(tb)),
             len(set(tb) - set(ta))))
    print("nonnull class casts: clang %d, edg-cir %d" % (NONNULL["clang"], NONNULL["edg-cir"]))
if args.global_order:
    def order(text):
        return [m.group(1) for m in re.finditer(
            r"cir\.global [^\n@]*@\"?([\w.$]+)\"?[^\n]*\bctor\b", text)]
    oa, ob = order(normalize(ours)), order(normalize(theirs))
    common = [n for n in ob if n in oa]
    if [n for n in oa if n in ob] != common:
        status = 1
        print("DIFF  global init order: clang %s, edg-cir %s" % (common,
              [n for n in oa if n in ob]))
    else:
        print("same  global init order %s" % common)
for key in sorted(set(a) ^ set(b)):
    print("only in %s: %s @%s" % ("edg-cir" if key in a else "clang", *key))
sys.exit(status)
