# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# ECIL fixture generator helpers for test/cirgen (expressions are lists of
# lines in ecil-asm text).  Run a generator from this directory.
import sys
INT, U64, S64, CHAR, UCHAR, SHORT, USHORT, UINT, FLT, DBL, VOID, VPTR, CPTR, IPTR, BOOL, LDBL = range(1, 17)
def K(t, v): return [f"CONST_INT {t} {v}"]
def KF(t, v): return [f"CONST_FLOAT {t} {v}"]
def L(l): return [f"LREF {l}", "LOAD 0"]
def Lr(l): return [f"LREF {l}"]
def B(op, a, b, fl=0): return a + b + [f"{op} {fl}"]
def ADD(a, b, fl=1): return B("ADD", a, b, fl)
def SUB(a, b, fl=1): return B("SUB", a, b, fl)
def MUL(a, b, fl=1): return B("MUL", a, b, fl)
def DIV(a, b, fl=1): return B("DIV", a, b, fl)
def REM(a, b, fl=0): return B("REM", a, b, fl)
def SHL(a, b, fl=0): return B("SHL", a, b, fl)
def SHR(a, b, fl=0): return B("SHR", a, b, fl)
def AND(a, b): return B("BIT_AND", a, b)
def OR(a, b): return B("BIT_OR", a, b)
def XOR(a, b): return B("BIT_XOR", a, b)
def CMPB(k, a, b): return a + b + [f"CMP {k}"]          # bool
def CMPI(k, a, b): return a + b + [f"CMP {k}", f"CAST 4 {INT}"]  # int
def cast(kind, t, a): return a + [f"CAST {kind} {t}"]
def integral(t, a): return cast(0, t, a)
def tobool(a): return a + ["TO_BOOL"]
def assign(l, e): return [f"LREF {l}"] + e + ["STORE 0 0"]
def ret(e): return e + ["RETURN_STMT true"]
def seq(*parts):
    out = []
    for p in parts: out += p
    return out
class Prog:
    def __init__(s, title, checks=(), lit_extra=()):
        s.title = title; s.checks = list(checks); s.decls = []; s.funcs = []; s.ngid = 20
        s.nf = 100; s.tests = []; s.lit_extra = list(lit_extra); s.strs = {}
    def gid(s):
        s.ngid += 1; return s.ngid
    def sid(s, text):
        if text not in s.strs:
            s.strs[text] = 10 + len(s.strs)
            s.decls.append(f'  STR {s.strs[text]} "{text}"')
        return s.strs[text]
    def func_type(s, ret, params, flags=0):
        g = s.gid(); s.decls.append(f"  TY_FUNC {g} {ret} {flags} [{', '.join(map(str, params))}]"); return g
    def ty(s, line):
        g = s.gid(); s.decls.append("  " + line.replace("@", str(g))); return g
    def decl_func(s, name, tfunc, flags=1, linkage=0):
        g = s.nf; s.nf += 1
        s.decls.append(f"  DECL_FUNC {g} {tfunc} {linkage} {flags} {s.sid(name)}"); return g
    def body(s, lines): s.funcs.append(lines)
    def test(s, name, stmts, locals_=(), params=()):
        """int name(void) test; locals_ = list of (l, type, align)."""
        if not hasattr(s, "t_void"): s.t_void = s.func_type(INT, [])
        g = s.decl_func(name, s.t_void)
        b = [f"  FUNC_BEGIN {g} 0"]
        for (l, t, a) in locals_: b.append(f"  LOCAL {l} {t} nil {a} 0")
        b += ["  " + x for x in seq(*stmts)]
        b.append("  FUNC_END 0"); s.funcs.append(b); s.tests.append(g); return g
    def render(s, path, expected):
        """expected: list of expected ints for s.tests in order. Also builds main."""
        if not hasattr(s, "t_void"): s.t_void = s.func_type(INT, [])
        gm = s.decl_func("main", s.t_void, flags=9)
        m = [f"  FUNC_BEGIN {gm} 0"]
        for i, (g, e) in enumerate(zip(s.tests, expected)):
            m += ["  " + x for x in [f"CALL {g} {s.t_void} 0 0", f"CONST_INT 1 {e}", "CMP 5", "IF_BEGIN 0", f"CONST_INT 1 {i+1}", "RETURN_STMT true", "IF_END"]]
        m += ["  CONST_INT 1 0", "  RETURN_STMT true", "  FUNC_END 0"]
        m = [x if x.startswith("  ") else "  " + x for x in m]
        out = [f"; {l}" for l in s.title.strip().splitlines()]
        out += ["; RUN: cat %S/Inputs/header.txt %s > %t.txt",
                "; RUN: %ecil-asm %t.txt -o %t.ecil",
                "; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s",
                "; RUN: %edg-cir --emit=obj -o %t.o %t.ecil",
                "; RUN: gcc %t.o -o %t.exe -lm",
                "; RUN: %t.exe", ""]
        out += [f"; CHECK-DAG: {c}" for c in (s.checks or ["cir.func"])]
        out += ["", "frame DECLS"] + s.decls + ["frame REGION"]
        for f in s.funcs: out += f
        out += m + ["frame END", "  END auto 0", ""]
        open(path, "w").write("\n".join(out))
import struct
def fl(t, v):
    if t == FLT: return "f32:0x%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    return "f64:0x%016x" % struct.unpack("<Q", struct.pack("<d", v))[0]
def KF(t, v): return [f"CONST_FLOAT {t} {fl(t, v)}"]
def KL(v):  # x87 long double from a python float
    import math
    if v == 0: return ["CONST_FLOAT 16 fbits:10:0x00000000000000000000"]
    m, e = math.frexp(abs(v)); mant = int(m * (1 << 64)); ex = e - 1 + 16383
    sign = 0x8000 if v < 0 else 0
    return ["CONST_FLOAT 16 fbits:10:0x%04x%016x" % (sign | ex, mant)]
def fbody(g, params, locals_, stmts, end=0):
    """params: (l, abi, decl); locals_: (l, type, align)."""
    b = [f"  FUNC_BEGIN {g} 0"]
    for (l, a, d) in params: b.append(f"  PARAM {l} {a} {d} nil 0")
    for (l, t, a) in locals_: b.append(f"  LOCAL {l} {t} nil {a} 0")
    b += ["  " + x for x in seq(*stmts)]
    b.append(f"  FUNC_END {end}")
    return b
def CALL(g, t, args, fl=0):
    return seq(*args) + [f"CALL {g} {t} {len(args)} {fl}"]
def CALLI(callee, t, args, fl=0):
    return callee + seq(*args) + [f"CALL_INDIRECT {t} {len(args)} {fl}"]
