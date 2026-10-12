#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""K7 of M4a: is the fact set of the ECIL 1.2 class records enough to
reproduce clang's CIR record types for polymorphic classes?

This is an executable specification: a port of clang's CIRRecordLowering
(clang/lib/CIR/CodeGen/CIRGenRecordLayoutBuilder.cpp: lower, accumulateVPtrs,
accumulateBases, accumulateVBases, determinePacked, insertPadding) and of
CIRGenTypes::computeRecordLayout's choice of a ".base" record, working only
from the facts a writer can stream:

  size, align, nvsize, polymorphic, vptr offset (own vptr), the direct bases in
  declaration order (with virtual flag and offset), the primary base and
  whether it is virtual, and for every member its offset and type.

The facts are read from `clang -cc1 -fdump-record-layouts`; the CIR record
types they give are compared with the ones `clang -fclangir -emit-cir`
produces for the same file.  It needs no LLVM library, only the pin's clang.

usage: layout_m4.py [--show] FILE.cpp     (inside cir-env)
"""
import argparse, re, subprocess, sys

CLANG = "/edg/llvm-cir/bin/clang"
TRIPLE = "x86_64-unknown-linux-gnu"

SCALARS = {"char": ("!s8i", 1), "signed char": ("!s8i", 1),
           "unsigned char": ("!u8i", 1), "short": ("!s16i", 2),
           "unsigned short": ("!u16i", 2), "int": ("!s32i", 4),
           "unsigned int": ("!u32i", 4), "long": ("!s64i", 8),
           "unsigned long": ("!u64i", 8), "long long": ("!s64i", 8),
           "float": ("!cir.float", 4), "double": ("!cir.double", 8),
           "bool": ("!cir.bool", 1)}


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.stdout + r.stderr


# ---------------------------------------------------------------- the facts

class Cls:
    def __init__(self, name):
        self.name = name
        self.size = self.dsize = self.align = self.nvsize = self.nvalign = 0
        self.vptr = False          # own vptr at offset 0
        self.bases = []            # (cls name, virtual, offset, primary, empty)
        self.fields = []           # (offset, type text, name, empty)
        self.is_class = False
        self.primary = None        # (name, virtual)

    @property
    def dynamic(self):
        return self.vptr or self.primary is not None or any(
            b[1] for b in self.bases) or any(True for b in self.bases if b[5])


def parse_dump(text):
    """-> {name: Cls}."""
    classes = {}
    for block in text.split("*** Dumping AST Record Layout\n")[1:]:
        block = block.split("*** Dumping IRgen Record Layout")[0]
        lines = block.rstrip("\n").split("\n")
        head = re.match(r"\s*0 \| (struct|class|union) (\S+)", lines[0])
        if not head or head.group(1) == "union":
            continue
        c = Cls(head.group(2))
        c.is_class = head.group(1) == "class"
        depth_of = lambda sp: (len(sp) - 1) // 2
        for line in lines[1:]:
            m = re.match(r"\s*\| \[sizeof=(\d+), dsize=(\d+), align=(\d+),?", line)
            if m:
                c.size, c.dsize, c.align = map(int, m.groups())
                continue
            m = re.match(r"\s*\|\s+nvsize=(\d+), nvalign=(\d+)", line)
            if m:
                c.nvsize, c.nvalign = map(int, m.groups())
                continue
            m = re.match(r"\s*(\d+) \|( +)(.*)$", line)
            if not m or depth_of(m.group(2)) != 1:
                continue            # only the direct children of the record
            off, text_ = int(m.group(1)), m.group(3)
            empty = "(empty)" in text_
            text_ = text_.replace("(empty)", "").strip()
            if re.match(r"^\(\S+ vtable pointer\)$", text_):
                c.vptr = True
                continue
            mb = re.match(r"^(struct|class) (\S+) \((primary virtual base|"
                          r"primary base|virtual base|base)\)$", text_)
            if mb:
                kind = mb.group(3)
                virtual = "virtual" in kind
                primary = kind.startswith("primary")
                c.bases.append((mb.group(2), virtual, off, primary, empty))
                if primary:
                    c.primary = (mb.group(2), virtual)
                continue
            ma = re.match(r"^(.*?)\s+(\w+)((?:\[\d+\])*)$", text_)
            if not ma:
                continue
            tname, fname, dims = ma.group(1), ma.group(2), ma.group(3)
            tname = re.sub(r"^(struct|class) ", "", tname)
            md = re.match(r"^(.*?)((?:\[\d+\])+)$", tname)
            if md:                       # clang prints "char[5] c"
                tname, dims = md.group(1), md.group(2) + dims
            c.fields.append((off, tname, fname, dims, empty))
        classes[c.name] = c
    return classes


# ---------------------------------------------------------------- type model

class Ty:
    def __init__(self, text, size, align):
        self.text, self.size, self.align = text, size, align


def u8_array(n):
    return Ty("!u8i" if n == 1 else "!cir.array<!u8i x %d>" % n, n, 1)


def struct_ty(name, members, packed, is_class=False):
    """members: [(kind, Ty)] -> Ty with the CIR printing and DataLayout."""
    body = ", ".join("%s %s" % (k, t.text) for k, t in members)
    text = '!cir.struct<%s"%s" %s{%s}>' % (
        "class " if is_class else "", name, "packed " if packed else "", body)
    if packed:
        size = sum(t.size for _, t in members)
        align = 1
    else:
        align = max([t.align for _, t in members] + [1])
        off = 0
        for _, t in members:
            off = -(-off // t.align) * t.align + t.size
        size = -(-off // align) * align
    return Ty(text, size, align)


def lower_struct(c, classes, types, nonvirtual_base, packed_in):
    """CIRRecordLowering::lower for a non-union class.  Returns (members,
    packed) with members [(kind, Ty)]."""
    size = c.nvsize if nonvirtual_base else c.size
    mem = []          # (offset, Ty or None, kind, is_marker)

    def storage_type(name):
        return base_subobject_type(name, classes, types)

    # accumulateFields
    for off, tname, fname, dims, empty in c.fields:
        if tname in classes:
            t = Ty("!rec_" + tname.replace(".", "2E"), 0, 0)
            t = complete_type(tname, classes, types)
            mem.append((off, t, "data"))
        else:
            txt, sz = SCALARS[tname]
            t = Ty(txt, sz, sz)
        for d in re.findall(r"\[(\d+)\]", dims):
            n = int(d)
            t = Ty("!cir.array<%s x %d>" % (t.text, n), t.size * n, t.align)
        if tname not in classes or dims:
            mem.append((off, t, "data"))
    # accumulateVPtrs
    own_vptr = c.vptr
    if own_vptr:
        mem.append((0, Ty("!cir.vptr", 8, 8), "data"))
    # accumulateBases
    for (bn, virtual, off, primary, empty) in c.bases:
        if primary and virtual:
            mem.append((0, storage_type(bn), "data"))
    for (bn, virtual, off, primary, empty) in c.bases:
        if virtual:
            continue
        b = classes[bn]
        if empty or b.nvsize == 0:
            continue
        mem.append((off, storage_type(bn), "data"))
    if not mem:
        return [("pad", u8_array(size))], False
    # accumulateVBases (complete type only)
    if not nonvirtual_base:
        vbases = collect_vbases(c.name, classes)
        for bn in vbases:
            b = classes[bn]
            if is_empty_for_layout(bn, classes):
                continue
            off = vbase_offset(c, bn, classes)
            # ItaniumCXXABI::isNearlyEmpty: empty, or non-virtual size == one
            # pointer (a record that is a primary base is always dynamic)
            near_empty = b.nvsize in (0, 8)
            if near_empty and not has_own_storage(c.name, bn, classes):
                continue            # lives inside the base it is primary of
            mem.append((off, storage_type(bn), "data"))
    mem.sort(key=lambda m: m[0])      # stable
    sentinel = (size, Ty("!u8i", 1, 1), "data")
    members = mem + [sentinel]
    # determinePacked
    packed = packed_in
    if not packed:
        alignment = nv_alignment = 1
        nv_size = c.nvsize if not nonvirtual_base else 0
        for off, t, k in mem:
            if off % t.align:
                packed = True
            if off < nv_size:
                nv_alignment = max(nv_alignment, t.align)
            alignment = max(alignment, t.align)
        if members[-1][0] % alignment:
            packed = True
        if nv_size % nv_alignment:
            packed = True
        if not packed:
            members[-1] = (members[-1][0], Ty("!u%di" % (alignment * 8), alignment, alignment), "data")
    # insertPadding
    pads = []
    pos = 0
    for off, t, k in members:
        natural = pos if packed else -(-pos // t.align) * t.align
        if off != natural:
            pads.append((pos, off - pos))
        pos = off + t.size
    allm = members + [(p, u8_array(n), "pad") for p, n in pads]
    allm.sort(key=lambda m: m[0])
    return [(k, t) for off, t, k in allm[:-1]], packed


def collect_vbases(name, classes):
    """All virtual bases, in clang's order (CXXRecordDecl::vbases(): a depth
    first walk of the declared bases, virtual bases first reached first).
    Non-recursive."""
    result, seen = [], set()
    stack = [(name, 0)]
    # emulate the recursive collection: for each base in order, a virtual base
    # is recorded when reached, and its own virtual bases follow it.
    order = []
    def children(n):
        return [b for b in classes[n].bases]
    work = [(name, iter(children(name)))]
    while work:
        n, it = work[-1]
        nxt = next(it, None)
        if nxt is None:
            work.pop()
            continue
        bn, virtual, off, primary, empty = nxt
        if virtual and bn not in seen:
            seen.add(bn)
            result.append(bn)
        work.append((bn, iter(children(bn))))
    return result


def vbase_offset(c, bn, classes):
    for b in c.bases:
        if b[0] == bn and b[1]:
            return b[2]
    # a virtual base reached through a base: find it in the base's own facts
    # (the offset in the complete object is the one printed in this class's
    # dump, nested; the writer streams complete-object offsets).
    return c.vbase_offsets[bn]


def has_own_storage(decl, query, classes):
    """CodeGenUtils::hasOwnStorage, non-recursively."""
    stack = [decl]
    while stack:
        d = stack.pop()
        dc = classes[d]
        if dc.primary and dc.primary[1] and dc.primary[0] == query:
            return False
        for b in dc.bases:
            stack.append(b[0])
    return True


def is_empty_for_layout(name, classes):
    c = classes[name]
    if c.dynamic:
        return False
    return c.size == 1 and not c.fields and all(
        is_empty_for_layout(b[0], classes) for b in c.bases)


def complete_type(name, classes, types):
    key = ("c", name)
    if key not in types:
        types[key] = build_complete(name, classes, types)
    return types[key]


def base_subobject_type(name, classes, types):
    key = ("b", name)
    if key not in types:
        c = classes[name]
        if c.nvsize != c.size:
            members, packed = lower_struct(c, classes, types, True, complete_packed(name, classes, types))
            t = struct_ty(name + ".base", members, packed)
            t.text = t.text
            types[key] = Ty("!rec_" + name + "2Ebase", t.size, t.align)
            types[("bdef", name)] = t
        else:
            types[key] = complete_type(name, classes, types)
    return types[key]


def complete_packed(name, classes, types):
    complete_type(name, classes, types)
    return types[("packed", name)]


def build_complete(name, classes, types):
    c = classes[name]
    members, packed = lower_struct(c, classes, types, False, False)
    t = struct_ty(name, members, packed, c.is_class)
    types[("packed", name)] = packed
    types[("def", name)] = t
    return Ty("!rec_" + name, t.size, t.align)


# ------------------------------------------------------------- comparison

def cir_types(text):
    out = {}
    for m in re.finditer(r'^!rec_\w+ = (!cir\.struct<(?:class )?"([^"]+)" .*>)$',
                         text, re.M):
        body = re.sub(r"!rec_(\w+)", lambda x: "!rec_" + x.group(1), m.group(1))
        out[m.group(2)] = body
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()
    dump = run([CLANG, "-cc1", "-triple", TRIPLE, "-x", "c++", "-std=c++17",
                "-fdump-record-layouts", "-fsyntax-only", args.source])
    classes = parse_dump(dump)
    # complete-object offsets of virtual bases reached through bases: take them
    # from the nested lines of the dump (virtual base lines at any depth)
    for block in dump.split("*** Dumping AST Record Layout\n")[1:]:
        block = block.split("*** Dumping IRgen Record Layout")[0]
        lines = block.rstrip("\n").split("\n")
        head = re.match(r"\s*0 \| (struct|class) (\S+)", lines[0])
        if not head or head.group(2) not in classes:
            continue
        c = classes[head.group(2)]
        c.vbase_offsets = {}
        for line in lines[1:]:
            m = re.match(r"\s*(\d+) \|\s+(?:struct|class) (\S+) \((?:primary )?virtual base\)", line)
            if m:
                c.vbase_offsets.setdefault(m.group(2), int(m.group(1)))
    ref = run([CLANG, "-fclangir", "-fno-exceptions", "-emit-cir", "-Xclang",
               "-clangir-disable-passes", args.source, "-o", "-"])
    theirs = cir_types(ref)
    types = {}
    ours = {}
    for n in classes:
        complete_type(n, classes, types)
        base_subobject_type(n, classes, types)
    for key, t in types.items():
        if key[0] == "def":
            ours[key[1]] = t.text
        elif key[0] == "bdef":
            ours[key[1] + ".base"] = t.text
    norm = lambda s: re.sub(r"!rec_(\w+?)2Ebase", lambda m: "!rec_" + m.group(1) + "2Ebase", s)
    bad = 0
    names = sorted(set(ours) & set(theirs))
    for n in names:
        a, b = norm(ours[n]), norm(theirs[n])
        same = a == b
        bad += not same
        print("%-6s %s" % ("MATCH" if same else "DIFF", n))
        if args.show or not same:
            print("   clang: " + b)
            print("   facts: " + a)
    print("%d types compared, %d differ; only in clang: %s; only in facts: %s" % (
        len(names), bad, sorted(set(theirs) - set(ours)), sorted(set(ours) - set(theirs))))
    return 1 if bad else 0


sys.exit(main())
