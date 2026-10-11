#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Tag the differences cir-compare.py --cxx --fe reports.

usage: cir-diff-classify.py [--table] FILE.cmp...   (box_diff.sh output)

Every "ops:" delta line of a DIFF function gets one tag (the first rule that
matches); a function carries the set of its tags.  Tags name the benign
categories of doc/research/cxx_cir_differential.md; a line no rule matches is
tagged "other" and printed, those are the candidates for a real difference.
The call-sequence line, the record types and the global initializers are
checked by cir-compare.py itself (DIFFER / DIFF type / DIFF global lines are
listed here too).
"""
import re
import sys

RULES = [
    # writer / CIRGen bookkeeping
    ("retval", r'__retval|cir\.return|cir\.trap|\^bb|cir\.unreachable'),
    ("scope", r"cir\.scope|cir\.cleanup\.scope|^\} cleanup|\} cleanup normal"),
    ("exprcleanup", r"tmp\.exprcleanup|extract\.tmp"),
    ("nrvo-flag", r'"nrvo"'),
    ("temp", r'cir\.alloca "(TMP|tmp|agg|ref|w)\b|%@(TMP|tmp)'),
    ("signature-attrs", r"cir\.func"),
    ("label-names", r"cir\.(goto|label)"),
    # shapes of the same thing
    ("array-index", r"array_to_ptrdecay|cast bitcast % : !cir\.ptr<!cir\.array"
                    r"|get_element|ptr_stride|cast integral|ptr_diff"),
    ("base-cast", r"cast bitcast .*rec_\w+(\.base)?>|get_member %\[\d+\] "
                  r"\{name = \"[be]\"\}"),
    ("struct-copy", r"libc\.memcpy|cir\.copy|aSERKS_"),
    ("fe-folded", r"cir\.(add|sub|mul|and|or|neg|not|shift|cmp|select)\b"
                  r"|cir\.const|bool_to_int|int_to_bool|cir\.condition"),
    ("cond-shape", r"cir\.if|cir\.ternary|\} else|\}, false|\}\) : \(|"
                   r"cir\.yield"),
    ("address", r"cir\.get_global|cir\.get_member|cir\.load|cir\.store"
                r"|cir\.alloca"),
    # alloc attributes and array-new shapes
    ("alloc-attrs", r"allocsize|builtin\}|builtin,|cir\.call @(malloc|_Zd[la]\w*)\("),
    ("array-new", r"array\.ctor|array\.dtor|_Znam|_Znwm|align_val_t|"
                  r"overflow|cir\.delete_array|cir\.select|cast bitcast % : "
                  r"!cir\.ptr<(!u8i|!void|!rec_\w+)>"),
    # lambdas
    ("lambda-names", r"UliE|UlvE|UlT_|_FUN|\$_\d|__invoke|cvPF"),
    # structor variants
    ("structor", r"_Z\w*[CD]x[\w]*\("),
]


def tag_of(line):
    for tag, rx in RULES:
        if re.search(rx, line):
            return tag
    return "other"


def main():
    args = sys.argv[1:]
    table = False
    if args and args[0] == "--table":
        table = True
        args = args[1:]
    total = {}
    others = []
    for path in args:
        name = path.split("/")[-1].replace("tests_cir_cirgen_", "") \
                   .replace("spike_cxx-shapes_", "s:").replace(".cmp", "")
        funcs = {}
        cur = None
        same = diff_funcs = 0
        extra = []
        for line in open(path, errors="replace"):
            m = re.match(r"^(DIFF|same)  (func|global|type) (\S+)", line)
            if m:
                cur = None
                if m.group(2) == "func":
                    if m.group(1) == "same":
                        same += 1
                    else:
                        diff_funcs += 1
                        cur = m.group(3)
                        funcs[cur] = set()
                elif m.group(1) == "DIFF":
                    extra.append("%s %s" % (m.group(2), m.group(3)))
                continue
            if cur is None:
                continue
            m = re.match(r"^  ops: (.*)$", line)
            if m and "same multiset" not in m.group(1):
                body = m.group(1).split(": ", 1)[1] if ": " in m.group(1) \
                    else m.group(1)
                t = tag_of(body)
                funcs[cur].add(t)
                if t == "other":
                    others.append((name, cur, m.group(1)))
            elif m:
                funcs[cur].add("order")
            elif line.startswith("  calls: DIFFER"):
                funcs[cur].add("CALLS")
        counts = {}
        for fn, tags in funcs.items():
            for t in tags:
                counts[t] = counts.get(t, 0) + 1
        print("%-28s same=%-3d diff=%-3d %s%s" % (
            name, same, diff_funcs,
            " ".join("%s=%d" % kv for kv in sorted(counts.items())),
            ("  [" + ", ".join(extra) + "]") if extra else ""))
        total["same"] = total.get("same", 0) + same
        total["diff"] = total.get("diff", 0) + diff_funcs
        for k, v in counts.items():
            total[k] = total.get(k, 0) + v
    print("TOTAL " + " ".join("%s=%d" % kv for kv in sorted(total.items())))
    for name, fn, l in others:
        print("other  %s %s  %s" % (name, fn, l[:140]))


main()
