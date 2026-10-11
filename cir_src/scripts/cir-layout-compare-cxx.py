#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compare EDG-CIRGen's record lowering of C++ classes with clang's CIRGen.

M3a spike 7 (base subobjects).  Like cir-layout-compare.py, but for classes
with bases, empty bases and tail padding: the facts (size, dsize, align,
offsets, base lists) come from clang -fdump-record-layouts; every class is
encoded as ECIL, a class used as a base whose dsize is smaller than its size
gets a second record "<name>.base" of size dsize (what ECIL v1 expresses with
REC_FIELD BASE/POTENTIALLY_OVERLAPPING and the library synthesizes), and the
CIR record types edg-cir produces are compared with the ones clang -fclangir
-emit-cir produces for the same file.  Only scalar members, nested classes
and (empty) bases are supported.

usage: cir-layout-compare-cxx.py [--show] FILE.cpp   (inside cir-env)
"""
import argparse, os, re, subprocess, sys, tempfile

CLANG = "/edg/llvm-cir/bin/clang"
TRIPLE = "x86_64-unknown-linux-gnu"
BUILD = os.environ.get("EDG_CIR_BUILD", "build/cir")
HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "test", "cirgen", "Inputs", "header.txt")
SCALARS = {"char": 4, "signed char": 4, "unsigned char": 5, "short": 6,
           "unsigned short": 7, "int": 1, "unsigned int": 8, "long": 3,
           "unsigned long": 2, "float": 9, "double": 10, "_Bool": 15}


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout + r.stderr


def parse(text):
    """-> ordered {name: {size, dsize, align, items}}; item = dict(kind
    base|empty_base|member|empty_member|scalar, offset, type, name)."""
    recs = {}
    for block in text.split("*** Dumping AST Record Layout\n")[1:]:
        block = block.split("*** Dumping IRgen Record Layout")[0]
        lines = block.rstrip("\n").split("\n")
        head = re.match(r"\s*0 \| (struct|union|class) (\S+)", lines[0])
        if not head:
            continue
        rec = {"kind": head.group(1), "name": head.group(2), "items": [],
               "empty": "(empty)" in lines[0]}
        for line in lines[1:]:
            m = re.match(r"\s*\| \[sizeof=(\d+), dsize=(\d+), align=(\d+)",
                         line)
            if m:
                rec["size"], rec["dsize"], rec["align"] = map(int, m.groups())
                continue
            m = re.match(r"\s*(\d+) \| (\s*)(.*)$", line)
            if not m or len(m.group(2)) != 2:
                continue            # members of nested records are inline
            off, text_ = int(m.group(1)), m.group(3)
            empty = "(empty)" in text_
            text_ = text_.replace("(empty)", "").strip()
            mb = re.match(r"^(struct|class) (\S+) \(base\)$", text_)
            if mb:
                rec["items"].append({"kind": "empty_base" if empty else "base",
                                     "offset": off, "type": mb.group(2)})
                continue
            mm = re.match(r"^(struct|class) (\S+) (\w+)$", text_)
            if mm:
                rec["items"].append({"kind": "member", "offset": off,
                                     "type": mm.group(2), "name": mm.group(3),
                                     "empty": empty})
                continue
            ms = re.match(r"^(.*?)\s+(\w+)$", text_)
            rec["items"].append({"kind": "scalar", "offset": off,
                                 "type": ms.group(1), "name": ms.group(2)})
        recs[rec["name"]] = rec
    return recs


def encode(recs):
    lines = []
    gid = {}
    sids = {}
    nxt = [100]

    def new():
        nxt[0] += 1
        return nxt[0]

    def sid(text):
        if text not in sids:
            sids[text] = new()
            lines.append('  STR %d "%s"' % (sids[text], text))
        return sids[text]
    used_as_base = {i["type"] for r in recs.values() for i in r["items"]
                    if i["kind"] in ("base", "empty_base")}
    base_gid = {}

    def body(name, g, rec, size, dsize, align):
        lines.append("  TY_RECORD_DECL %d 0 %d" % (g, sid(name)))
        fields = []
        for it in rec["items"]:
            if it["kind"] == "empty_base":
                continue
            if it["kind"] == "base":
                b = it["type"]
                t = base_gid.get(b, gid[b])
                fields.append("  REC_FIELD %d %d nil nil nil 0" %
                              (t, it["offset"]))
            elif it["kind"] == "member":
                fl = 2 if it.get("empty") else 0
                fields.append("  REC_FIELD %d %d nil nil %d %d" %
                              (gid[it["type"]], it["offset"],
                               sid(it["name"]), fl))
            else:
                fields.append("  REC_FIELD %d %d nil nil %d 0" %
                              (SCALARS[it["type"]], it["offset"],
                               sid(it["name"])))
        lines.append("  TY_RECORD_BODY %d %d %d %d 0 %d" %
                     (g, size, dsize, align, len(fields)))
        lines.extend(fields)

    for name, rec in recs.items():
        if rec["size"] == 1 and not rec["items"]:
            # an empty class: one byte of padding, no fields
            g = new()
            gid[name] = g
            lines.append("  TY_RECORD_DECL %d 0 %d" % (g, sid(name)))
            lines.append("  TY_RECORD_BODY %d 1 0 1 0 0" % g)
            continue
        if name in used_as_base and rec["dsize"] < rec["size"]:
            bg = new()
            body(name + ".base", bg, rec, rec["dsize"], rec["dsize"],
                 rec["align"])
            base_gid[name] = bg
        g = new()
        gid[name] = g
        body(name, g, rec, rec["size"], rec["dsize"], rec["align"])
    for name in recs:
        v = new()
        lines.append('  DECL_VAR %d %d 0 1 4 %d' % (v, gid[name],
                                                     sid("g_" + name)))
    return lines


def cir_types(text):
    out = {}
    for m in re.finditer(r'^!rec_\w+ = !cir\.struct<"([^"]+)" (.*)>$', text,
                         re.M):
        body = re.sub(r"!rec_(\w+)", lambda x: x.group(1), m.group(2))
        out[m.group(1)] = body
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--clang", default=CLANG)
    ap.add_argument("--ecil-asm", default=os.path.join(
        BUILD, "tools/ecil-asm/ecil-asm"))
    ap.add_argument("--edg-cir", default=os.path.join(BUILD, "bin/edg-cir"))
    args = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="cirlayoutcxx.")
    dump = run([args.clang, "-cc1", "-triple", TRIPLE, "-x", "c++",
                "-fdump-record-layouts", "-fsyntax-only", args.source])
    recs = parse(dump)
    stream = open(HEADER).read() + "frame DECLS\n" + \
        "\n".join(encode(recs)) + "\nframe END\n  END auto 0\n"
    txt = os.path.join(tmp, "s.txt")
    open(txt, "w").write(stream)
    ecil = os.path.join(tmp, "s.ecil")
    r = subprocess.run([args.ecil_asm, txt, "-o", ecil], capture_output=True,
                       text=True)
    if r.returncode:
        sys.exit("ecil-asm: " + r.stderr)
    ours = cir_types(run([args.edg_cir, "--emit=cir", ecil]))
    ref_cir = run([args.clang, "-fclangir", "-fno-exceptions", "-emit-cir",
                   "-Xclang", "-clangir-disable-passes", args.source, "-o",
                   "-"])
    # The clang module only types what it uses: add a use of every class.
    clangs = cir_types(ref_cir)
    bad = 0
    names = sorted(set(ours) & set(clangs))
    for n in names:
        same = ours[n] == clangs[n]
        bad += not same
        print("%-8s %s" % ("MATCH" if same else "DIFF", n))
        if args.show or not same:
            print("   clang: " + clangs[n])
            print("   edg  : " + ours[n])
    print("%d types compared, %d differ; only in clang: %s" %
          (len(names), bad, sorted(set(clangs) - set(ours))))
    return 1 if bad else 0


sys.exit(main())
