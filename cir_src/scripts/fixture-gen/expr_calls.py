# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""Calls: direct, K&R/no_proto call-site types, re-issued declarations (F3),
indirect, record arguments/results through CallConvLowering, mixed fp.""",
 ["cir.call @add2({{.*}}) nothrow : (!s32i, !s32i) -> !s32i", "cir.cast bitcast {{.*}} !cir.ptr<!cir.func<(...) -> !s32i>>", "cir.call {{%[0-9]+}}(", "cir.func {{.*}}@knr"])
exp = []
Tp = p.func_type(INT, [INT, INT])
Tnp0 = p.func_type(INT, [], 2)
Tknr = p.func_type(INT, [INT, INT], 2)
Ti1 = p.func_type(INT, [INT])
Tvar = p.func_type(INT, [INT], 1)
Tdd = p.func_type(DBL, [INT, DBL, FLT])
PFN = p.ty(f"TY_PTR @ {Tp}")
S = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {S} 8 8 4 0 2", "  REC_FIELD 1 0 nil nil nil 0", "  REC_FIELD 1 4 nil nil nil 0"]
Tmk = p.func_type(S, [INT, INT]); Tsum = p.func_type(INT, [S])
BIGARR = p.ty("TY_ARRAY @ 3 4 0")
BIG = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {BIG} 32 32 8 0 1", f"  REC_FIELD {BIGARR} 0 nil nil nil 0"]
Tbig = p.func_type(S3 := S, [BIG]) if False else p.func_type(INT, [BIG])
Tmkbig = p.func_type(BIG, [INT])
add2 = p.decl_func("add2", Tp)
knr = p.decl_func("knr", Tnp0, flags=0)
late = p.decl_func("late", Ti1, flags=0)
mk = p.decl_func("mk", Tmk); sumP = p.decl_func("sumP", Tsum)
mix = p.decl_func("mix", Tdd)
sumBig = p.decl_func("sumBig", Tbig); mkBig = p.decl_func("mkBig", Tmkbig)
P = lambda l: Lr(l)
def sf(l, i, rec=S): return [f"LREF {l}", f"FIELD {rec} {i}"]
frames = []
def region(body): frames.append(body)
body = []
body += fbody(add2, [(1,1,1),(2,1,1)], [], [ret(ADD(L(1),L(2)))])
body += fbody(mk, [(1,1,1),(2,1,1)], [(3,S,4)], [[*sf(3,0)]+L(1)+["STORE 0 0"], [*sf(3,1)]+L(2)+["STORE 0 0"], ret(Lr(3)+["LOAD 0"])])
body += fbody(sumP, [(1,S,S)], [], [ret(ADD(seq(sf(1,0),["LOAD 0"]), MUL(seq(sf(1,1),["LOAD 0"]),K(1,10))))])
body += fbody(mix, [(1,1,1),(2,DBL,DBL),(3,FLT,FLT)], [], [ret(ADD(cast(6,DBL,L(1)), MUL(L(2),cast(8,DBL,L(3)),0),0))])
# sumBig(big b): b.x[0] + b.x[3]*10 ; mkBig(n): x[i] = n+i
def bx(l, i): return seq(Lr(l), [f"FIELD {BIG} 0", "ADDR", f"CAST 12 {p.ptr3}"] if False else [f"FIELD {BIG} 0"])
p.ptr3 = None
# helpers to address big.x[i]: LREF l FIELD BIG 0 -> glv array; ADDR; CAST decay; PTR_STRIDE
P64 = p.ty("TY_PTR @ 3")
def bxi(l, i): return seq(Lr(l), [f"FIELD {BIG} 0", "ADDR", f"CAST 12 {P64}"], K(1,i), ["PTR_STRIDE 0", "DEREF"])
body += fbody(sumBig, [(1,BIG,BIG)], [], [ret(cast(0,1,ADD(seq(bxi(1,0),["LOAD 0"]), MUL(seq(bxi(1,3),["LOAD 0"]),K(3,10)),0)))])
st = [seq(bxi(1,i), cast(0,3,ADD(L(2),K(1,i))), ["STORE 0 0"]) for i in range(4)]
body += fbody(mkBig, [(2,1,1)], [(1,BIG,8)], st + [ret(Lr(1)+["LOAD 0"])])
region(body)
# use_late: calls late(5) through the prototyped type int(int); late is later
# redefined as int late(int, ...).
tvoid = p.func_type(INT, [])
def addtest(name, stmts, locals_, v):
    g = p.decl_func(name, tvoid); p.tests.append(g); exp.append(v)
    return fbody(g, [], locals_, stmts)
frame2 = []
frame2 += addtest("t_direct", [ret(CALL(add2,Tp,[CALL(add2,Tp,[K(1,1),K(1,2)]), CALL(add2,Tp,[K(1,3),K(1,4)])]))], [], 10)
frame2 += addtest("t_late", [ret(CALL(late,Ti1,[K(1,5)]))], [], 11)
frame2 += addtest("t_knr", [ret(CALL(knr,Tnp0,[K(1,3),K(1,300)]))], [], 47)
frame2 += addtest("t_indirect", [assign(1, ["GREF %d" % add2]), ret(CALLI(L(1),Tp,[K(1,20),K(1,22)]))], [(1,PFN,8)], 42)
frame2 += addtest("t_record", [assign(1, CALL(mk,Tmk,[K(1,3),K(1,4)])), ret(CALL(sumP,Tsum,[Lr(1)+["LOAD 0"]]))], [(1,S,4)], 43)
frame2 += addtest("t_record_direct", [ret(CALL(sumP,Tsum,[CALL(mk,Tmk,[K(1,5),K(1,6)])]))], [], 65)
frame2 += addtest("t_big", [assign(1, CALL(mkBig,Tmkbig,[K(1,7)])), ret(CALL(sumBig,Tbig,[Lr(1)+["LOAD 0"]]))], [(1,BIG,8)], 7+100)
frame2 += addtest("t_mixed", [ret(cast(7,1,CALL(mix,Tdd,[K(1,2),KF(DBL,1.5),KF(FLT,4.0)])))], [], 8)
region(frame2)
# late redefinition as variadic, K&R definition of knr (re-issued declarations)
frame3 = ["frame DECLS", f"  DECL_FUNC {late} {Tvar} 0 1 {p.sid('late')}", f"  DECL_FUNC {knr} {Tknr} 0 1 {p.sid('knr')}", "frame REGION"]
frame3 += fbody(late, [(1,1,1)], [], [ret(MUL(L(1),K(1,2),1)+K(1,1)+["ADD 1"])])
frame3 += fbody(knr, [(1,1,SHORT),(2,1,CHAR)], [], [ret(ADD(integral(1,L(1)), integral(1,L(2))))])
# build: frames order: region(add2..), decls/regions
p.funcs = []
p.funcs = [frames[0] + ["frame REGION"] + frames[1] + frame3]
p.render(D+"expr-calls.test", exp)
