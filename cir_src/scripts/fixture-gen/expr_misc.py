# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates test/cirgen/expr-misc.test: corpus-driven regressions
# (pointer/integer bitcasts, non-null pointer constants in bodies, weak
# declarations).
import os
from lib import *
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""Regressions from the C corpus: a bitcast cast between pointer and integer,
a non-null pointer constant stored by INIT_CONST, a weak function that is
only declared (extern_weak, null at run time).""",
  ["cir.cast ptr_to_int", "cir.cast int_to_ptr", "extern_weak", "cir.cast int_to_bool", "cir.load align(1) {{.*}} !cir.long_double"])
exp = []
Tf = p.func_type(INT, [INT])
weak = p.decl_func("undefined_weak", Tf, flags=0, linkage=5)
def T(name, stmts, v, locals_=()):
    p.test(name, stmts, locals_); exp.append(v)
T("m_bitcast", [assign(1, Lr(2) + ["ADDR"]), ret(CMPI(4, cast(9, IPTR, cast(10, U64, L(1))), L(1)))], 1, [(1, IPTR, 8), (2, 1, 4)])
T("m_ptr_const", [Lr(1) + [f"C_PTR {IPTR} 4096", "INIT_CONST"], ret(CMPI(4, cast(10, S64, L(1)), K(S64, 4096)))], 1, [(1, IPTR, 8)])
T("m_weak_decl", [ret(cast(4, 1, cast(3, BOOL, [f"GREF {weak}"])))], 0)
# two returns of different struct locals through an sret pointer
A4 = p.ty("TY_ARRAY @ 3 4 0")
BIG = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {BIG} 32 32 8 0 1", f"  REC_FIELD {A4} 0 nil nil nil 0"]
Tpick = p.func_type(BIG, [INT]); pick = p.decl_func("pick", Tpick)
P64 = p.ty("TY_PTR @ 3")
def bx0(l): return seq(Lr(l), [f"FIELD {BIG} 0", "ADDR", f"CAST 12 {P64}", "DEREF"])
body = fbody(pick, [(1, 1, 1)], [(2, BIG, 8), (3, BIG, 8)],
    [seq(bx0(2), K(3, 11), ["STORE 0 0"]), seq(bx0(3), K(3, 22), ["STORE 0 0"]),
     seq(L(1), ["TO_BOOL", "IF_BEGIN 0"], ret(Lr(2) + ["LOAD 0"]), ["IF_END"]),
     ret(Lr(3) + ["LOAD 0"])])
Tt = p.func_type(INT, [])
g = p.decl_func("m_two_returns", Tt); p.tests.append(g); exp.append(2211)
body += fbody(g, [], [(1, BIG, 8), (2, BIG, 8)],
    [assign(1, CALL(pick, Tpick, [K(1, 1)])), assign(2, CALL(pick, Tpick, [K(1, 0)])),
     ret(ADD(MUL(cast(0, 1, seq(bx0(2), ["LOAD 0"])), K(1, 100)), cast(0, 1, seq(bx0(1), ["LOAD 0"]))))])
p.funcs = [body] + p.funcs
# K&R _Bool parameter: the ABI type is int, the declared type _Bool
Tknrb = p.func_type(INT, [INT], 2)
Tnp0 = p.func_type(INT, [], 2)
knrb = p.decl_func("knr_bool", Tknrb)
body2 = fbody(knrb, [(1, INT, BOOL)], [], [ret(cast(4, 1, L(1)))])
g = p.decl_func("m_knr_bool", p.func_type(INT, [])); p.tests.append(g); exp.append(1)
body2 += fbody(g, [], [], [ret(ADD(CALL(knrb, Tnp0, [K(1, 5)]), CALL(knrb, Tnp0, [K(1, 0)])))])
# a packed record with a 16-byte member, accessed through a pointer: the
# access alignment is the record's (1), not the member's natural 16
PK = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {PK} 17 17 1 1 2", "  REC_FIELD 4 0 nil nil nil 0", f"  REC_FIELD {LDBL} 1 nil nil nil 0"]
PPK = p.ty(f"TY_PTR @ {PK}")
g = p.decl_func("m_packed", p.func_type(INT, [])); p.tests.append(g); exp.append(3)
body2 += fbody(g, [], [(1, PK, 1), (2, PPK, 8)],
    [assign(2, Lr(1) + ["ADDR"]),
     seq(L(2), ["DEREF", f"FIELD {PK} 1"], KL(3.0), ["STORE 0 0"]),
     ret(cast(7, 1, seq(L(2), ["DEREF", f"FIELD {PK} 1", "LOAD 0"])))])
p.funcs = [body2] + p.funcs
p.render(D + "expr-misc.test", exp)
