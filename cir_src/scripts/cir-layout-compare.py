#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compare EDG-CIRGen record layouts against clang's CIRGen.

For every struct/union in a C corpus, take the front-end facts (size,
alignment, field byte/bit offsets and widths) from clang's
-fdump-record-layouts, encode them as ECIL, run edg-cir --emit=cir, and
compare the resulting CIR record types and global alignments with the CIR
that clang -fclangir -emit-cir produces for the same corpus.

usage: cir-layout-compare.py [--show NAME] CORPUS.C   (inside cir-env)
"""
import argparse, os, re, subprocess, sys, tempfile

CLANG = "/edg/llvm-cir/bin/clang"
TRIPLE = "x86_64-unknown-linux-gnu"
BUILD = os.environ.get("EDG_CIR_BUILD", "build/cir")
HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "test", "cirgen", "Inputs", "header.txt")

SCALARS = {  # clang type text -> (gid in header.txt, signed)
    "char": (4, True), "signed char": (4, True), "unsigned char": (5, False),
    "short": (6, True), "unsigned short": (7, False),
    "int": (1, True), "unsigned int": (8, False),
    "long": (3, True), "unsigned long": (2, False),
    "long long": (3, True), "unsigned long long": (2, False),
    "float": (9, True), "double": (10, True), "long double": (16, True),
    "_Bool": (15, False),
}
PACKED = {"S07", "S08", "S26", "S31"}


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout + r.stderr


def parse_layouts(text):
    """-> ordered list of dicts: key, kind, size, dsize, align, fields."""
    records = []
    blocks = text.split("*** Dumping AST Record Layout\n")[1:]
    for block in blocks:
        block = block.split("*** Dumping IRgen Record Layout")[0]
        lines = block.rstrip("\n").split("\n")
        head = re.match(r"\s*0 \| (struct|union) (.*)$", lines[0])
        if not head:
            continue
        rec = {"kind": head.group(1), "key": head.group(2), "fields": []}
        for line in lines[1:]:
            m = re.match(r"\s*\| \[sizeof=(\d+)(?:, dsize=(\d+))?, align=(\d+)",
                         line)
            if m:
                rec["size"] = int(m.group(1))
                rec["dsize"] = int(m.group(2) or m.group(1))
                rec["align"] = int(m.group(3))
                continue
            m = re.match(r"\s*(\d+)(?::(\d+)-(\d+))? \| (\s*)(.*)$", line)
            if not m:
                continue
            indent = len(m.group(4))
            if indent != 2:
                continue  # members of nested records are listed inline
            rec["fields"].append({
                "byte": int(m.group(1)),
                "bit": None if m.group(2) is None else int(m.group(2)),
                "width": None if m.group(2) is None
                else int(m.group(3)) - int(m.group(2)) + 1,
                "text": m.group(5)})
        records.append(rec)
    return records


def parse_ast_fields(text):
    """-> {record tag: [(name|None, clang type, width|None), ...]} for the
    named records of the corpus (all declared fields, zero-width included)."""
    out = {}
    cur = None
    last = None
    for line in text.split("\n"):
        m = re.match(r"^([|` -]*)RecordDecl 0x\w+ <[^>]*>(?: [^ ]*)* "
                     r"(struct|union) (\w+) definition", line)
        if m and len(m.group(1)) == 2:
            cur = m.group(3)
            out[cur] = []
            last = None
            continue
        if cur is None:
            continue
        m = re.match(r"^([|` -]*)FieldDecl 0x\w+ <[^>]*> \S+ (?:(\w+) )?"
                     r"'([^']*)'", line)
        if m and len(m.group(1)) == 4:
            last = [m.group(2), m.group(3), None]
            out[cur].append(last)
            continue
        m = re.search(r"value: Int (\d+)", line)
        if m and last is not None and last[2] is None:
            last[2] = int(m.group(1))
    return out


class Encoder:
    def __init__(self):
        self.lines = []
        self.next_gid = 100
        self.types = {}     # type text -> gid
        self.records = {}   # key -> gid
        self.strs = {}
        self.next_sid = 100
        self.sizes = {}

    def gid(self):
        self.next_gid += 1
        return self.next_gid

    def sid(self, text):
        if text not in self.strs:
            self.next_sid += 1
            self.strs[text] = self.next_sid
            self.lines.append('  STR %d "%s"' % (self.next_sid, text))
        return self.strs[text]

    def split_field(self, text, bitfield):
        """'unsigned int b' -> (type text, name or None)."""
        m = re.match(r"^(.*?)(?:\s+(\w+))?$", text)
        body, name = m.group(1), m.group(2)
        # A trailing word that is part of the type, not a name.
        words = {"char", "short", "int", "long", "float", "double", "_Bool",
                 "unsigned", "signed"}
        if name in words or (name and name in
                             {k.split()[-1] for k in self.records}
                             and re.search(r"(struct|union)$", body)):
            body, name = (body + " " + name).strip(), None
        if re.search(r"(struct|union)$", body) and name:
            body, name = body + " " + name, None
        return body.strip(), name

    def type_gid(self, text):
        text = re.sub(r"\b(volatile|const|restrict)\b\s*", "", text).strip()
        if text in SCALARS:
            return SCALARS[text][0]
        m = re.match(r"^(.*?)\s*\*$", text)
        if m:
            key = text
            if key not in self.types:
                inner = self.type_gid(m.group(1)) if m.group(1) != "void" else 11
                g = self.gid()
                self.lines.append("  TY_PTR %d %d" % (g, inner))
                self.types[key] = g
            return self.types[key]
        m = re.match(r"^(.*?) \(\*\)\((.*)\)$", text)
        if m:
            if text not in self.types:
                ret = self.type_gid(m.group(1)) if m.group(1) != "void" else 11
                params = [] if m.group(2) in ("", "void") else \
                    [self.type_gid(x.strip()) for x in m.group(2).split(",")]
                gf = self.gid()
                self.lines.append("  TY_FUNC %d %d 0 [%s]" %
                                  (gf, ret, ", ".join(map(str, params))))
                g = self.gid()
                self.lines.append("  TY_PTR %d %d" % (g, gf))
                self.types[text] = g
            return self.types[text]
        m = re.match(r"^(.*?)\s*\[(\d*)\]$", text)
        if m:
            elem = self.type_gid(m.group(1))
            if text in self.types:
                return self.types[text]
            g = self.gid()
            count = m.group(2)
            if count == "":
                self.lines.append("  TY_ARRAY %d %d nil 1" % (g, elem))
            elif count == "0":
                self.lines.append("  TY_ARRAY %d %d 0 2" % (g, elem))
            else:
                self.lines.append("  TY_ARRAY %d %d %s 0" % (g, elem, count))
            self.types[text] = g
            return g
        m = re.match(r"^(struct|union) (.*)$", text)
        if m and m.group(2) in self.records:
            return self.records[m.group(2)]
        raise ValueError("unknown type text: %r" % text)

    def add_record(self, rec, ast_fields=None):
        g = self.gid()
        kind = 1 if rec["kind"] == "union" else 0
        tag = "nil"
        if "(unnamed" not in rec["key"]:
            tag = str(self.sid(rec["key"]))
        self.lines.append("  TY_RECORD_DECL %d %d %s" % (g, kind, tag))
        self.records[rec["key"]] = g
        self.sizes[rec["key"]] = rec["size"]
        fields = []
        flags = 0
        name = rec["key"]
        if name in PACKED:
            flags |= 1
        fields_in = list(rec["fields"])
        if ast_fields and any(w == 0 for (_, _, w) in ast_fields):
            merged = []
            it = iter(rec["fields"])
            for k, (nm_, tx_, w_) in enumerate(ast_fields):
                if w_ == 0:
                    nxt = None
                    for j in range(k + 1, len(ast_fields)):
                        if ast_fields[j][2] != 0:
                            nxt = j
                            break
                    # byte offset: that of the next real field (or the size)
                    real_before = sum(1 for x in ast_fields[:k] if x[2] != 0)
                    off = (rec["fields"][real_before]["byte"]
                           if real_before < len(rec["fields"]) else rec["size"])
                    merged.append({"byte": off, "bit": 0, "width": 0,
                                   "text": tx_})
                else:
                    merged.append(next(it))
            fields_in = merged
        for f in fields_in:
            bitfield = f["bit"] is not None
            tx, nm = self.split_field(f["text"], bitfield)
            ff = 0
            if nm is None:
                ff |= 1
            if tx.endswith("[]"):
                ff |= 8
                flags |= 2
            t = self.type_gid(tx)
            if f["bit"] is None and (tx.endswith("[0]") or (
                    tx.split(" ", 1)[0] in ("struct", "union") and
                    self.sizes.get(tx.split(" ", 1)[1]) == 0)):
                ff |= 2   # empty for the ABI: zero-length array, empty record
            sid = "nil" if nm is None else str(self.sid(nm))
            if bitfield:
                if SCALARS.get(tx, (0, False))[1]:
                    ff |= 4
                fields.append("  REC_FIELD %d %d %d %d %s %d" %
                              (t, f["byte"], f["bit"], f["width"], sid, ff))
            else:
                fields.append("  REC_FIELD %d %d nil nil %s %d" %
                              (t, f["byte"], sid, ff))
        self.lines.append("  TY_RECORD_BODY %d %d %d %d %d %d" %
                          (g, rec["size"], rec["dsize"], rec["align"], flags,
                           len(fields)))
        self.lines += fields
        return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--show", action="append", default=[])
    ap.add_argument("--out", help="directory for the intermediate files")
    ap.add_argument("--clang", default=CLANG)
    ap.add_argument("--ecil-asm", default=os.path.join(
        BUILD, "tools/ecil-asm/ecil-asm"))
    ap.add_argument("--edg-cir", default=os.path.join(BUILD, "bin/edg-cir"))
    args = ap.parse_args()
    tmp = args.out or tempfile.mkdtemp(prefix="cirlayout.")
    os.makedirs(tmp, exist_ok=True)

    layout_text = run([args.clang, "-cc1", "-triple", TRIPLE,
                       "-fdump-record-layouts", "-emit-llvm-only",
                       args.corpus])
    recs = parse_layouts(layout_text)
    ast_text = run([args.clang, "-cc1", "-triple", TRIPLE, "-ast-dump",
                    args.corpus])
    ast = parse_ast_fields(ast_text)
    enc = Encoder()
    enc.lines.append("frame DECLS")
    named = []
    for rec in recs:
        g = enc.add_record(rec, ast.get(rec["key"].split()[-1])
                           if "(unnamed" not in rec["key"] else None)
        if "(unnamed" not in rec["key"] and "::" not in rec["key"]:
            named.append((rec, g))
    for rec, g in named:
        vname = "g_" + rec["key"]
        enc.lines.append("  DECL_VAR %d %d 0 33 %d %d" %
                         (enc.gid(), g, rec["align"], enc.sid(vname)))
    header = open(HEADER).read()
    asm = header + "\n".join(enc.lines) + "\nframe END\n  END auto 0\n"
    asm_path = os.path.join(tmp, "layout.txt")
    open(asm_path, "w").write(asm)
    ecil = os.path.join(tmp, "layout.ecil")
    r = subprocess.run([args.ecil_asm,
                        asm_path, "-o", ecil], capture_output=True, text=True)
    if r.returncode:
        print("ecil-asm failed:", r.stderr)
        return 2
    mine = run([args.edg_cir, "--emit=cir", ecil])
    theirs = run([args.clang, "-cc1", "-triple", TRIPLE, "-fclangir", "-emit-cir",
                  "-clangir-disable-passes", args.corpus, "-o", "-"])
    open(os.path.join(tmp, "mine.cir"), "w").write(mine)
    open(os.path.join(tmp, "theirs.cir"), "w").write(theirs)

    def rec_types(text):
        out = {}
        for m in re.finditer(r"^(!\S+) = !cir\.(struct|union)<"
                             r"\"?([^\"{ ]*)\"?\s*(.*)\}>", text, re.M):
            out[m.group(3)] = m.group(4)
        return out

    def globals_of(text):
        out = {}
        for m in re.finditer(r"cir\.global [^@]*@(g_\w+) = [^\n]*?: (\S+?)"
                             r"(?: align\((\d+)\))?(?:\s+loc\(.*\))?\s*(?:\{|$)", text, re.M):
            out[m.group(1)] = (m.group(2), m.group(3))
        return out

    def norm(s):
        return re.sub(r"!rec_[\w.]+", "!rec", s)

    def layouts_of(text):
        return {m.group(1): m.group(2) for m in re.finditer(
            r"(\w[\w.]*) = #cir\.record_layout<([^>]*)>", text)}

    a, b = rec_types(mine), rec_types(theirs)
    la, lb = layouts_of(mine), layouts_of(theirs)
    ga, gb = globals_of(mine), globals_of(theirs)
    bad = 0
    for rec, _ in named:
        key = rec["key"]
        n = key.split()[-1]
        ok = True
        notes = []
        ra, rb = a.get(n), b.get(n)
        if ra is None or rb is None:
            notes.append("missing: mine=%s theirs=%s" % (ra is not None,
                                                        rb is not None))
            ok = False
        elif norm(ra) != norm(rb):
            notes.append("record differs:\n    mine:   %s\n    theirs: %s" %
                         (norm(ra), norm(rb)))
            ok = False
        if la.get(n) != lb.get(n):
            notes.append("record_layout: mine=%s theirs=%s" %
                         (la.get(n), lb.get(n)))
            ok = False
        ma, mb = ga.get("g_" + n), gb.get("g_" + n)
        if ma is None or mb is None or ma[1] != mb[1]:
            notes.append("global align: mine=%s theirs=%s" % (ma, mb))
            ok = False
        if not ok or n in args.show:
            print(("DIFF " if not ok else "show ") + n)
            for x in notes:
                print("  " + x)
            if ok:
                print("    " + norm(ra))
        bad += not ok
    print("%d records, %d differ" % (len(named), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
