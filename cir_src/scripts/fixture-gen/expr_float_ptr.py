# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
# ---- floats
p = Prog("Floating point: arithmetic, conversions, comparisons (NaN), x87 long double.",
  ["cir.fadd {{.*}} : !cir.double", "cir.fmul {{.*}} : !cir.float", "cir.fdiv", "cir.fsub", "cir.fneg", "cir.cast float_to_int", "cir.cast int_to_float", "cir.cast floating", "cir.cast float_to_bool", "!cir.long_double<!cir.f80>"])
exp = []
def T(name, e, v, locals_=()):
    p.test(name, [ret(e)], locals_); exp.append(v)
f2i = lambda a: cast(7, 1, a)
T("f_arith", f2i(SUB(ADD(MUL(KF(DBL,1.5),KF(DBL,4.0),0), DIV(KF(DBL,2.5),KF(DBL,0.5),0),0), KF(DBL,1.0),0)), 10)
T("f_single", f2i(MUL(ADD(KF(FLT,0.5),KF(FLT,0.25),0), KF(FLT,4.0),0)), 3)
T("f_conv", ADD(f2i(MUL(DIV(cast(6,DBL,K(1,7)),KF(DBL,2.0),0),KF(DBL,2.0),0)), f2i(KF(DBL,-3.9))), 4)
T("f_floating", CMPI(5, cast(8,DBL,cast(8,FLT,KF(DBL,0.1))), KF(DBL,0.1)), 1)
nan = lambda: DIV(KF(DBL,0.0),KF(DBL,0.0),0)
T("f_nan", ADD(CMPI(4,nan(),nan()), MUL(CMPI(5,nan(),nan()),K(1,10))), 10)
T("f_cmp", ADD(CMPI(0,KF(DBL,1.0),KF(DBL,2.0)), MUL(CMPI(3,KF(DBL,1.0),KF(DBL,2.0)),K(1,5))), 1)
T("f_neg_bool", cast(4,1,cast(2,15,ADD(seq(KF(DBL,2.5),["NEG 0"]), KF(DBL,3.0),0))), 1)
T("f_ldbl", f2i(MUL(KL(1.5),KL(2.0),0)), 3)
T("f_ldbl_conv", ADD(f2i(cast(8,LDBL,KF(DBL,2.5))), cast(7,1,cast(6,LDBL,K(1,4)))), 6)
p.render(D+"expr-float.test", exp)
# ---- pointers
p = Prog("Pointers: stride, difference, byte offsets, array decay, int<->ptr, compare.",
  ["cir.ptr_stride", "cir.ptr_diff", "cir.cast array_to_ptrdecay", "cir.cast int_to_ptr", "cir.cast ptr_to_int", "cir.cast ptr_to_bool"])
ARR = p.ty("TY_ARRAY @ 1 4 0")
exp = []
def decay(l): return seq(Lr(l), ["ADDR", f"CAST 12 {IPTR}"])
def init(l):
    out = []
    for i, v in enumerate([10,20,30,40]):
        out += assign_elem(l, i, v)
    return out
def assign_elem(l, i, v):
    return seq(decay(l), K(1,i), ["PTR_STRIDE 0", "DEREF"], K(1,v), ["STORE 0 0"])
def deref(e): return e + ["DEREF", "LOAD 0"]
loc = [(1, ARR, 16), (2, IPTR, 8)]
def T(name, stmts, v, locals_=loc):
    p.test(name, [init(1)] + stmts, locals_); exp.append(v)
T("p_stride", [ret(ADD(ADD(deref(seq(decay(1),K(1,2),["PTR_STRIDE 0"])), deref(seq(decay(1),K(1,3),["PTR_STRIDE 0"]))), deref(seq(decay(1),K(1,3),["PTR_STRIDE 0"],K(1,2),["PTR_STRIDE 1"]))))], 90)
T("p_diff", [ret(cast(0,1,seq(decay(1),K(1,3),["PTR_STRIDE 0"],decay(1),[f"PTR_DIFF {S64}"])))], 3)
T("p_unsigned_index", [ret(deref(seq(decay(1),K(8,1),["PTR_STRIDE 2"])))], 20)
T("p_byte_offset", [ret(deref(seq(decay(1),[f"ADDR_OFFSET {IPTR} 8"])))], 30)
T("p_ptrint", [ret(ADD(deref(cast(9,IPTR,cast(10,U64,decay(1)))), ADD(CMPI(2,seq(decay(1),K(1,1),["PTR_STRIDE 0"]),decay(1)), ADD(CMPI(4,decay(1),[f"CONST_NULL {IPTR}"]), cast(4,1,cast(3,15,decay(1)))))))], 12)
T("p_store_through", [assign(2, decay(1)), seq(L(2),["DEREF"],K(1,7),["STORE 0 0"]), ret(seq(L(2),["DEREF","LOAD 0"], decay(1), ["DEREF","LOAD 0"],["ADD 1"]))], 14)
p.render(D+"expr-ptr.test", exp)
