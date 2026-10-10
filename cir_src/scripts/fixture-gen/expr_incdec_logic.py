# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
def ID(kind, l, fl=0): return [f"LREF {l}", f"INC_DEC {kind} {fl}"]
# ---- inc/dec, store results
p = Prog("INC_DEC on int/uchar/short/pointer/volatile and the STORE result modes.",
  ["cir.inc nsw", "cir.inc {{%[0-9]+}} : !u8i", "cir.dec nsw", "cir.ptr_stride", "volatile"])
ARR = p.ty("TY_ARRAY @ 1 4 0")
exp = []
def T(name, stmts, v, locals_):
    p.test(name, stmts, locals_); exp.append(v)
T("i_int", [assign(1,K(1,5)), ret(ADD(ADD(ADD(MUL(ID(2,1,1),K(1,1000)), MUL(ID(0,1,1),K(1,100))), MUL(ID(3,1,1),K(1,10))), ID(1,1,1)))], 5775, [(1,1,4)])
T("i_uchar", [assign(1,K(5,255)), ret(ADD(integral(1,ID(2,1)), MUL(integral(1,ID(0,1)),K(1,1000))))], 1255, [(1,UCHAR,1)])
T("i_short", [assign(1,K(6,32767)), ret(integral(1,ID(0,1)))], -32768, [(1,SHORT,2)])
def decay(l): return seq(Lr(l), ["ADDR", f"CAST 12 {IPTR}"])
def init(l): return [seq(decay(l), K(1,i), ["PTR_STRIDE 0","DEREF"], K(1,v), ["STORE 0 0"]) for i,v in enumerate([10,20,30,40])]
def deref(e): return e + ["DEREF","LOAD 0"]
T("i_ptr", init(1) + [assign(2,decay(1)), ret(ADD(ADD(MUL(deref(ID(2,2)),K(1,100)), deref(Lr(2)+["LOAD 0"])), deref(ID(1,2))))], 1030, [(1,ARR,16),(2,IPTR,8)])
T("i_volatile", [["LREF 1"]+K(1,1)+["STORE 1 0"], ["LREF 1","INC_DEC 0 2","DROP"], ret(["LREF 1","LOAD 1"])], 2, [(1,1,4)])
T("store_prv", [assign(2, seq(Lr(1),K(1,5),["STORE 0 2"],K(1,1),["ADD 1"])), ret(ADD(L(2),MUL(L(1),K(1,100))))], 506, [(1,1,4),(2,1,4)])
T("store_glv", [ret(seq(Lr(1),K(1,4),["STORE 0 1","LOAD 0"]))], 4, [(1,1,4)])
p.render(D+"expr-incdec.test", exp)
# ---- logic
p = Prog("LAND/LOR/TERN: short circuit, int/pointer/void/record results.",
  ["cir.ternary", "cir.ternary", "cir.struct", "cir.bool"])
SREC = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls.append(f"  TY_RECORD_BODY {SREC} 8 8 4 0 2")
p.decls.append(f"  REC_FIELD 1 0 nil nil nil 0")
p.decls.append(f"  REC_FIELD 1 4 nil nil nil 0")
exp = []
def land(a, b): return tobool(a) + ["LAND_BEGIN"] + tobool(b) + ["LAND_END"]
def lor(a, b): return tobool(a) + ["LOR_BEGIN"] + tobool(b) + ["LOR_END"]
def tern(c, t, a, b): return tobool(c) + [f"TERN_BEGIN {t}"] + a + ["TERN_ELSE"] + b + ["TERN_END"]
setx = lambda l,v: seq(Lr(l), K(1,v), ["STORE 0 2"])
def logic(name, e, v):   # r = e; return r*10 + x
    T(name, [ret(ADD(MUL(cast(4,1,e),K(1,10)), L(1)))], v, [(1,1,4)])
    # x starts uninitialised: zero it first
    p.funcs[-1].insert(2, "  LREF 1\n  CONST_INT 1 0\n  STORE 0 0")
logic("l_and_short", land(K(1,0), setx(1,1)), 0)
logic("l_and_full", land(K(1,1), setx(1,2)), 12)
logic("l_or_short", lor(K(1,1), setx(1,1)), 10)
logic("l_or_full", lor(K(1,0), setx(1,3)), 13)
logic("l_chain", lor(land(K(1,1),K(1,0)), land(K(1,1),setx(1,4))), 14)
T("t_int", [ret(ADD(tern(K(1,1),1,K(1,7),K(1,9)), MUL(tern(K(1,0),1,K(1,7),K(1,9)),K(1,100))))], 907, [])
T("t_ptr", [assign(1,K(1,1)), assign(2,K(1,2)), assign(3, tern(K(1,0),IPTR,Lr(1)+["ADDR"],Lr(2)+["ADDR"])), seq(L(3),["DEREF"],K(1,5),["STORE 0 0"]), ret(ADD(L(1),MUL(L(2),K(1,10))))], 50+1, [(1,1,4),(2,1,4),(3,IPTR,8)])
T("t_void", [assign(1,K(1,0)), seq(K(1,1),["TO_BOOL","TERN_BEGIN nil"],assign(1,K(1,1)),["TERN_ELSE"],assign(1,K(1,2)),["TERN_END"]), ret(L(1))], 1, [(1,1,4)])
def sfield(l, i): return [f"LREF {l}", f"FIELD {SREC} {i}"]
def sinit(l, a, b): return [seq(sfield(l,0),K(1,a),["STORE 0 0"]), seq(sfield(l,1),K(1,b),["STORE 0 0"])]
for c, v in ((0, 34), (1, 12)):
    T(f"t_record{c}", sinit(1,1,2) + sinit(2,3,4) + [seq(Lr(3), tern(K(1,c),SREC,Lr(1)+["LOAD 0"],Lr(2)+["LOAD 0"]), ["STORE 0 0"]), ret(ADD(MUL(seq(sfield(3,0),["LOAD 0"]),K(1,10)), seq(sfield(3,1),["LOAD 0"])))], v, [(1,SREC,4),(2,SREC,4),(3,SREC,4)])
p.render(D+"expr-logic.test", exp)
