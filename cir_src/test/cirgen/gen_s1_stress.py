#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Generates the ECIL text of the S1 stress streams (the stack of the process is
# the limit: the lit test runs the code generator under `ulimit -s 256`).
#
#   gen_s1_stress.py wide N     one class with N virtual functions
#   gen_s1_stress.py chain N    N classes, each derived from the previous one
#                               and overriding its virtual function, the last
#                               one with a constructor (VPTR_INIT) and every
#                               class with its vtable and typeinfo
# The header (Inputs/header12_cxx_rtti.txt) is not included.
import sys

class Ids:
    """Dense string and G-id allocators (the reader bounds a new id by twice
    the number introduced so far plus a slack)."""
    def __init__(self):
        self.next_sid = 100
        self.next_gid = 1000
        self.lines = []
    def string(self, text):
        self.next_sid += 1
        self.lines.append('  STR %d "%s"' % (self.next_sid, text))
        return self.next_sid
    def gid(self):
        self.next_gid += 1
        return self.next_gid
    def emit(self, line):
        self.lines.append("  " + line)

def mangled_member(cls, name, params):
    return "_ZN%d%s%d%sE%s" % (len(cls), cls, len(name), name, params)

def wide(n):
    ids = Ids()
    ids.lines.append("frame DECLS")
    s_name = ids.string("W"); s_mangle = ids.string("1W"); s_x = ids.string("x")
    s_c1 = ids.string("_ZN1WC1Ev"); s_c2 = ids.string("_ZN1WC2Ev")
    rec, ptr, fn_type = ids.gid(), ids.gid(), ids.gid()
    ids.emit("TY_VPTR 90")
    ids.emit("TY_RECORD_DECL %d 0 %d" % (rec, s_name))
    ids.emit("TY_RECORD_BODY %d 16 12 8 4 2" % rec)
    ids.emit("REC_FIELD 90 0 nil nil nil 1")
    ids.emit("REC_FIELD 1 8 nil nil %d 0" % s_x)
    ids.emit("CLASS_MANGLE %d %d" % (rec, s_mangle))
    ids.emit("CLASS_INFO %d 12 8 nil %d" % (rec, n))
    ids.emit("CLASS_BASES %d []" % rec)
    ids.emit("TY_PTR %d %d" % (ptr, rec))
    ids.emit("TY_FUNC %d 11 0 [%d]" % (fn_type, ptr))
    entries = []
    for i in range(n):
        sid = ids.string(mangled_member("W", "f%d" % i, "v"))
        g = ids.gid()
        ids.emit("DECL_FUNC %d %d 0 0 %d" % (g, fn_type, sid))
        entries.append("%d, %d, 0, %d" % (g, i, i + 1))
    c1, c2 = ids.gid(), ids.gid()
    ids.emit("DECL_FUNC %d %d 0 1 %d" % (c1, fn_type, s_c1))
    ids.emit("DECL_FUNC %d %d 0 1 %d" % (c2, fn_type, s_c2))
    ids.emit("STRUCTOR %d %d 0" % (c1, c2))
    ids.emit("CLASS_VFUNCS %d [%s]" % (rec, ", ".join(entries)))
    ids.emit("CLASS_OVERRIDES %d []" % rec)
    ids.emit("DECL_ABI_DATA %d 2 %d 0 1 nil" % (ids.gid(), rec))
    ids.emit("DECL_ABI_DATA %d 0 %d 0 1 nil" % (ids.gid(), rec))
    ids.lines.append("frame REGION")
    ids.emit("FUNC_BEGIN %d 0" % c2)
    ids.emit("PARAM 1 %d %d nil 1" % (ptr, ptr))
    ids.emit("VPTR_INIT")
    ids.emit("BLOCK_BEGIN")
    ids.emit("BLOCK_END")
    ids.emit("FUNC_END 1")
    ids.lines.append("frame END")
    ids.lines.append("  END auto 0")
    return "\n".join(ids.lines) + "\n"

def chain(n):
    ids = Ids()
    ids.lines.append("frame DECLS")
    ids.emit("TY_VPTR 90")
    s_x = ids.string("x")
    recs, ptrs, ftypes, fns = [], [], [], []
    for k in range(n):
        cname = "C%d" % k
        s_name = ids.string(cname)
        s_mangle = ids.string("%d%s" % (len(cname), cname))
        s_fn = ids.string(mangled_member(cname, "f", "v"))
        rec, ptr, fn_type, fn = ids.gid(), ids.gid(), ids.gid(), ids.gid()
        recs.append(rec); ptrs.append(ptr); ftypes.append(fn_type); fns.append(fn)
        ids.emit("TY_RECORD_DECL %d 0 %d" % (rec, s_name))
        if k == 0:
            ids.emit("TY_RECORD_BODY %d 16 12 8 4 2" % rec)
            ids.emit("REC_FIELD 90 0 nil nil nil 1")
            ids.emit("REC_FIELD 1 8 nil nil %d 0" % s_x)
        else:
            ids.emit("TY_RECORD_BODY %d 16 12 8 4 1" % rec)
            ids.emit("REC_FIELD %d 0 nil nil nil 16" % recs[k - 1])
        ids.emit("CLASS_MANGLE %d %d" % (rec, s_mangle))
        ids.emit("CLASS_INFO %d 12 8 %s 1" % (rec, "nil" if k == 0 else "0"))
        ids.emit("CLASS_BASES %d [%s]" % (rec, "" if k == 0 else "0, 1"))
        ids.emit("TY_PTR %d %d" % (ptr, rec))
        ids.emit("TY_FUNC %d 11 0 [%d]" % (fn_type, ptr))
        ids.emit("DECL_FUNC %d %d 0 1 %d" % (fn, fn_type, s_fn))
        ids.emit("CLASS_VFUNCS %d [%d, 0, 0, 1]" % (rec, fn))
        overrides = ["%d, 0, %d, 0" % (fns[j], fn) for j in range(k)]
        ids.emit("CLASS_OVERRIDES %d [%s]" % (rec, ", ".join(overrides)))
        ids.emit("DECL_ABI_DATA %d 2 %d 0 1 nil" % (ids.gid(), rec))
        ids.emit("DECL_ABI_DATA %d 0 %d 0 1 nil" % (ids.gid(), rec))
    last = n - 1
    s_c1 = ids.string("_ZN2CLC1Ev")
    s_c2 = ids.string("_ZN2CLC2Ev")
    c1, c2 = ids.gid(), ids.gid()
    ids.emit("DECL_FUNC %d %d 0 1 %d" % (c1, ftypes[last], s_c1))
    ids.emit("DECL_FUNC %d %d 0 1 %d" % (c2, ftypes[last], s_c2))
    ids.emit("STRUCTOR %d %d 0" % (c1, c2))
    ids.lines.append("frame REGION")
    ids.emit("FUNC_BEGIN %d 0" % c2)
    ids.emit("PARAM 1 %d %d nil 1" % (ptrs[last], ptrs[last]))
    ids.emit("VPTR_INIT")
    ids.emit("BLOCK_BEGIN")
    ids.emit("BLOCK_END")
    ids.emit("FUNC_END 1")
    ids.lines.append("frame END")
    ids.lines.append("  END auto 0")
    return "\n".join(ids.lines) + "\n"

if __name__ == "__main__":
    kind, n = sys.argv[1], int(sys.argv[2])
    sys.stdout.write(wide(n) if kind == "wide" else chain(n))
