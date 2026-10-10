# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""VA_START/ARG/END/COPY on x86-64 (array va_list), va_list passed to a
callee, and the v0 builtins.""",
 ["cir.va_start", "cir.va_arg", "cir.va_end", "cir.va_copy", "cir.expect", "cir.trap", "cir.unreachable"])
exp = []
TAG = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {TAG} 24 24 8 0 4", "  REC_FIELD 8 0 nil nil nil 0", "  REC_FIELD 8 4 nil nil nil 0", f"  REC_FIELD {VPTR} 8 nil nil nil 0", f"  REC_FIELD {VPTR} 16 nil nil nil 0"]
VAL = p.ty(f"TY_ARRAY @ {TAG} 1 0")
PTAG = p.ty(f"TY_PTR @ {TAG}")
S = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {S} 16 16 8 0 2", "  REC_FIELD 3 0 nil nil nil 0", "  REC_FIELD 3 8 nil nil nil 0"]
Tsum = p.func_type(INT, [INT], 1)
Tdsum = p.func_type(DBL, [INT], 1)
Tv = p.func_type(INT, [INT, PTAG])
Tmix = p.func_type(INT, [INT], 1)
Tvoid = p.func_type(INT, [])
Tabort = p.func_type(VOID, [])
sumv = p.decl_func("sumv", Tsum); dsum = p.decl_func("dsum", Tdsum)
vsum = p.decl_func("vsum", Tv); mixv = p.decl_func("mixv", Tmix); sumcp = p.decl_func("sumcp", Tsum)
abort_ = p.decl_func("abort", Tabort, flags=4)
def ap(l): return seq(Lr(l), ["ADDR", f"CAST 12 {PTAG}"])
def vaarg(apx, t): return apx + [f"VA_ARG {t}"]
def loop_sum(n_l, i_l, s_l, apx, t, conv=None):
    # for (i = 0; i < n; ++i) s += va_arg(ap, t)
    v = vaarg(apx, t)
    if conv: v = conv(v)
    return [seq(Lr(i_l),K(1,0),["STORE 0 0"]), seq(Lr(s_l),K(1,0),["STORE 0 0"]),
            seq(["FOR_BEGIN","FOR_COND true"], CMPB(0,L(i_l),L(n_l)), ["FOR_STEP true"], ID(0,i_l), ["DROP","FOR_BODY"], Lr(s_l), ["DUP","LOAD 0"], v, ["ADD 1","STORE 0 0","FOR_END"])]
def ID(k,l): return [f"LREF {l}", f"INC_DEC {k} 1"]
body = []
# int sumv(int n, ...) { va_list ap; va_start; sum ints; va_end; return s; }
body += fbody(sumv, [(1,1,1)], [(2,VAL,16),(3,1,4),(4,1,4)],
   [ap(2)+["VA_START"]] + loop_sum(1,3,4,ap(2),1) + [ap(2)+["VA_END"], ret(L(4))])
# double dsum(int n, ...)
body += fbody(dsum, [(1,1,1)], [(2,VAL,16),(3,1,4),(4,DBL,8)],
   [ap(2)+["VA_START"], seq(Lr(4),KF(DBL,0.0),["STORE 0 0"]), seq(Lr(3),K(1,0),["STORE 0 0"]),
    seq(["FOR_BEGIN","FOR_COND true"], CMPB(0,L(3),L(1)), ["FOR_STEP true"], ID(0,3), ["DROP","FOR_BODY"], Lr(4), ["DUP","LOAD 0"], vaarg(ap(2),DBL), ["ADD 0","STORE 0 0","FOR_END"]),
    ap(2)+["VA_END"], ret(L(4))])
# int vsum(int n, __va_list_tag *ap): sums n ints from a va_list pointer
body += fbody(vsum, [(1,1,1),(2,PTAG,PTAG)], [(3,1,4),(4,1,4)],
   loop_sum(1,3,4,L(2),1) + [ret(L(4))])
# int mixv(int n, ...): long, double, struct{long,long}, int, copy semantic
# returns n + va_arg(long)*10 + (int)va_arg(double) * 100 + s.a + s.b*1000? (small values)
body += fbody(mixv, [(1,1,1)], [(2,VAL,16),(3,S,8)],
   [ap(2)+["VA_START"], assign(3,vaarg(ap(2),S)),
    ret(ADD(ADD(ADD(L(1), MUL(cast(0,1,vaarg(ap(2),S64)),K(1,10))), MUL(cast(7,1,vaarg(ap(2),DBL)),K(1,100))), MUL(seq(Lr(3),[f"FIELD {S} 1"],["LOAD 0"],[f"CAST 0 1"]),K(1,1000)))) ] if False else
   [ap(2)+["VA_START"], seq(Lr(3),vaarg(ap(2),S),["STORE 0 0"]),
    ret(ADD(ADD(ADD(L(1), MUL(cast(0,1,vaarg(ap(2),S64)),K(1,10))), MUL(cast(7,1,vaarg(ap(2),DBL)),K(1,100))), MUL(seq(Lr(3),[f"FIELD {S} 1","LOAD 0","CAST 0 1"]),K(1,1000))))])
# int sumcp(int n, ...): va_copy; sum from the copy, then from the original;
# result = copy_sum*100 + orig_sum... both equal; return both: s1*1000+s2.
body += fbody(sumcp, [(1,1,1)], [(2,VAL,16),(5,VAL,16),(3,1,4),(4,1,4),(6,1,4)],
   [ap(2)+["VA_START"], ap(5)+ap(2)+["VA_COPY"]] + loop_sum(1,3,4,ap(5),1) + [assign(6,L(4)), ap(5)+["VA_END"]] + loop_sum(1,3,4,ap(2),1) + [ap(2)+["VA_END"], ret(ADD(MUL(L(6),K(1,1000)),L(4)))])
tv = p.func_type(INT, [])
def addtest(name, stmts, locals_, v):
    g = p.decl_func(name, tv); p.tests.append(g); exp.append(v)
    return fbody(g, [], locals_, stmts)
body += addtest("t_va_ints", [ret(CALL(sumv,Tsum,[K(1,3),K(1,10),K(1,20),K(1,30)],1))], [], 60)
body += addtest("t_va_many", [ret(CALL(sumv,Tsum,[K(1,8)]+[K(1,i) for i in range(1,9)],1))], [], 36)
body += addtest("t_va_double", [ret(cast(7,1,CALL(dsum,Tdsum,[K(1,3),KF(DBL,1.5),KF(DBL,2.5),KF(DBL,3.0)],1)))], [], 7)
body += addtest("t_va_ptr", [ret(CALL(vsum,Tv,[K(1,2), ap(2)]))], [(2,VAL,16)], 0) if False else []
# vsum via sumv-like wrapper: wrapper(int n, ...) { va_start; r = vsum(n, ap); va_end }
wrapper = p.decl_func("wrapper", Tsum)
body += fbody(wrapper, [(1,1,1)], [(2,VAL,16),(3,1,4)], [ap(2)+["VA_START"], assign(3,CALL(vsum,Tv,[L(1),ap(2)])), ap(2)+["VA_END"], ret(L(3))])
body += addtest("t_va_ptr", [ret(CALL(wrapper,Tsum,[K(1,3),K(1,4),K(1,5),K(1,6)],1))], [], 15)
body += addtest("t_va_mixed", [seq(Lr(1),[f"FIELD {S} 0"],K(3,7),["STORE 0 0"]), seq(Lr(1),[f"FIELD {S} 1"],K(3,8),["STORE 0 0"]), ret(CALL(mixv,Tmix,[K(1,1), Lr(1)+["LOAD 0"], K(3,3), KF(DBL,2.5)],1))], [(1,S,8)], 8231)
body += addtest("t_va_copy", [ret(CALL(sumcp,Tsum,[K(1,3),K(1,1),K(1,2),K(1,3)],1))], [], 6006)
# builtins
body += addtest("t_expect", [seq(Lr(1),K(1,0),["STORE 0 0"]), seq(cast(0,S64,L(1)), cast(0,S64,K(1,0)), ["BUILTIN 1 2 %d" % S64], cast(0,1,[]) if False else [], [f"CAST 0 1"], ["TO_BOOL","IF_BEGIN 0"], K(1,9), ["RETURN_STMT true","IF_END"]), ret(K(1,3))], [(1,1,4)], 3)
body += addtest("t_trap_untaken", [seq(Lr(1),K(1,0),["STORE 0 0"]), seq(L(1),["TO_BOOL","IF_BEGIN 0","BUILTIN 3 0 nil","IF_END"]), seq(L(1),["TO_BOOL","IF_BEGIN 0"],["CALL %d %d 0 2" % (abort_, Tabort)],["IF_END"]), ret(K(1,4))], [(1,1,4)], 4)
body += addtest("t_unreachable", [seq(Lr(1),K(1,5),["STORE 0 0"]), seq(L(1),["SWITCH_BEGIN","CASE_VALUE 5"],ret(K(1,50)),["CASE_DEFAULT","BUILTIN 2 0 nil","SWITCH_END"]), ["UNREACHABLE"]], [(1,1,4)], 50)
p.funcs = [body]
p.render(D+"expr-va.test", exp)
