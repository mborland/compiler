# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""Bit-field access: LOAD/STORE (get/set_bitfield), STORE result truncation,
INC_DEC on bit-fields, packed records.  The C companion (gcc) checks layout.""",
 ["cir.get_bitfield", "cir.set_bitfield"])
B = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {B} 16 16 8 0 4", "  REC_FIELD 1 0 0 3 nil 4","  REC_FIELD 1 0 3 5 nil 4","  REC_FIELD 8 1 0 20 nil 0","  REC_FIELD 3 8 0 40 nil 4"]
PB = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {PB} 6 6 1 1 4", "  REC_FIELD 4 0 nil nil nil 0","  REC_FIELD 1 1 0 4 nil 4","  REC_FIELD 1 1 4 20 nil 4","  REC_FIELD 6 4 nil nil nil 0"]
PBP = p.ty(f"TY_PTR @ {B}"); PPB = p.ty(f"TY_PTR @ {PB}")
Tfill = p.func_type(VOID, [PBP]); Tint = p.func_type(INT, [PBP])
Tfillpb = p.func_type(VOID, [PPB]); Tintpb = p.func_type(INT, [PPB])
fill = p.decl_func("fill", Tfill); sumf = p.decl_func("sum", Tint); incdec = p.decl_func("incdec", Tint)
store_res = p.decl_func("store_result", Tint); fillpb = p.decl_func("fillpb", Tfillpb); sumpb = p.decl_func("sumpb", Tintpb)
def fld(rec, i): return seq(L(1), ["DEREF", f"FIELD {rec} {i}"])
def setf(rec, i, t, v): return seq(fld(rec,i), K(t,v), ["STORE 0 0"])
def getf(rec, i): return seq(fld(rec,i), ["LOAD 0"])
body = []
body += fbody(fill, [(1,PBP,PBP)], [], [setf(B,0,1,3), setf(B,1,1,17), setf(B,2,8,1000000), setf(B,3,3,-5)], 1)
body += fbody(sumf, [(1,PBP,PBP)], [], [ret(ADD(ADD(ADD(getf(B,0),getf(B,1)),cast(0,1,getf(B,2))),cast(0,1,getf(B,3))))])
body += fbody(incdec, [(1,PBP,PBP)], [], [ret(seq(fld(B,0), ["INC_DEC 2 0"], fld(B,2), ["INC_DEC 0 0", "DROP"], fld(B,3), ["INC_DEC 1 0","DROP"]) if False else seq(fld(B,0), ["INC_DEC 2 0"])), ] if False else
   [seq(fld(B,2), ["INC_DEC 0 0", "DROP"]), seq(fld(B,3), ["INC_DEC 1 0", "DROP"]), ret(seq(fld(B,0), ["INC_DEC 2 0"], cast(0,1,[])))] if False else
   [seq(fld(B,2), ["INC_DEC 0 0", "DROP"]), seq(fld(B,3), ["INC_DEC 1 0", "DROP"]), ret(seq(fld(B,0), ["INC_DEC 2 0"]))])
body += fbody(store_res, [(1,PBP,PBP)], [(2,1,4)], [assign(2, seq(fld(B,0), K(1,5), ["STORE 0 2"])), ret(L(2))])
body += fbody(fillpb, [(1,PPB,PPB)], [], [setf(PB,0,4,9), setf(PB,1,1,-3), setf(PB,2,1,54321), setf(PB,3,6,77)], 1)
body += fbody(sumpb, [(1,PPB,PPB)], [], [ret(ADD(ADD(ADD(cast(0,1,getf(PB,0)),getf(PB,1)),getf(PB,2)),cast(0,1,getf(PB,3))))])
p.funcs = [body]
out = [f"; {l}" for l in p.title.strip().splitlines()]
out += ["; RUN: cat %S/Inputs/header.txt %s > %t.txt",
 "; RUN: %ecil-asm %t.txt -o %t.ecil",
 "; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s",
 "; RUN: %edg-cir --emit=obj -o %t.o %t.ecil",
 "; RUN: gcc %S/Inputs/bitfield_check.c %t.o -o %t.exe",
 "; RUN: %t.exe", ""]
out += [f"; CHECK-DAG: {c}" for c in p.checks]
out += ["", "frame DECLS"] + p.decls + ["frame REGION"] + body + ["frame END", "  END auto 0", ""]
open(D+"bitfield.test","w").write("\n".join(out))
