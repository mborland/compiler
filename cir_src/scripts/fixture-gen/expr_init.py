# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""Object initialization: AGG_INIT_* with zero fill, nested targets, INIT_CONST
(small and large aggregates), INIT_ZERO, MATERIALIZE_TEMP.""",
 ["cir.libc.memset", "@__const.t_init_big.0", "cir.copy"])
exp = []
S = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {S} 8 8 4 0 2", "  REC_FIELD 1 0 nil nil nil 0", "  REC_FIELD 1 4 nil nil nil 0"]
A3 = p.ty("TY_ARRAY @ 1 3 0")
T = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {T} 28 28 4 0 4", "  REC_FIELD 1 0 nil nil nil 0", "  REC_FIELD 4 4 nil nil nil 0", f"  REC_FIELD {A3} 8 nil nil nil 0", f"  REC_FIELD {S} 20 nil nil nil 0"]
A100 = p.ty("TY_ARRAY @ 1 100 0")
Tmk = p.func_type(S, [INT, INT])
mk = p.decl_func("mk", Tmk)
def sel(i): return [f"AGG_SEL {i}"]
def st(v, t=1): return K(t, v) + ["STORE 0 0"]
def f(l, rec, i): return [f"LREF {l}", f"FIELD {rec} {i}"]
def elem(l, i): return seq(Lr(l), ["ADDR", f"CAST 12 {p.ty('TY_PTR @ 1') if not hasattr(p,'pi') else p.pi}"], K(1,i), ["PTR_STRIDE 0","DEREF"])
p.pi = p.ty("TY_PTR @ 1")
def a3(l, i): return seq(f(l,T,2), ["ADDR", f"CAST 12 {p.pi}"], K(1,i), ["PTR_STRIDE 0","DEREF","LOAD 0"])
def tsum(l):   # a + c*10 + arr0*100 + arr1*1000 + arr2*10000 + s.a*100000 + s.b*1000000 wouldn't fit nicely; use weights up to 10^6 fits int
    w = [(seq(f(l,T,0),["LOAD 0"]),1), (cast(0,1,seq(f(l,T,1),["LOAD 0"])),1), (a3(l,0),1), (a3(l,1),1), (a3(l,2),1), (seq(f(l,T,3),[f"FIELD {S} 0","LOAD 0"]),1), (seq(f(l,T,3),[f"FIELD {S} 1","LOAD 0"]),1)]
    e = None
    for (x, _), m in zip(w, [1,1000,0,0,0,0,0]): pass
    return w
# check by comparing with a checksum: a + c + arr0*3 + arr1*5 + arr2*7 + s.a*11 + s.b*13
def checksum(l):
    w = tsum(l); coef = [1,1,3,5,7,11,13]
    e = w[0][0]
    for (x, _), c in zip(w[1:], coef[1:]): e = ADD(e, MUL(x, K(1,c)))
    return e
def t_agg(zero):
    init = [f"LREF 1", f"AGG_INIT_BEGIN {T} {zero}"] + sel(0) + st(1) + sel(1) + st(120,4) + sel(2) + [f"AGG_INIT_BEGIN {A3} {zero}"] + sel(0) + st(1) + sel(1) + st(2) + ["AGG_INIT_END"] + sel(3) + [f"AGG_INIT_BEGIN {S} {zero}"] + sel(0) + st(5) + sel(1) + st(6) + ["AGG_INIT_END", "AGG_INIT_END"]
    return init
def pre_dirty(l):   # fill with garbage first so zero fill is observable
    return [seq(f(l,T,0),K(1,99),["STORE 0 0"]), seq(f(l,T,2), ["ADDR", f"CAST 12 {p.pi}"], K(1,2), ["PTR_STRIDE 0","DEREF"], K(1,99), ["STORE 0 0"])]
def addtest(name, stmts, locals_, v):
    g = p.decl_func(name, p.func_type(INT, [])); p.tests.append(g); exp.append(v)
    return fbody(g, [], locals_, stmts)
body = []
body += fbody(mk, [(1,1,1),(2,1,1)], [(3,S,4)], [seq(f(3,S,0),L(1),["STORE 0 0"]), seq(f(3,S,1),L(2),["STORE 0 0"]), ret(Lr(3)+["LOAD 0"])])
# {1,'x',{1,2},{5,6}} with arr[2] zero-filled: 1+120+3+10+0+55+78 = 267
body += addtest("t_agg_zero", pre_dirty(1) + [t_agg(1), ret(checksum(1))], [(1,T,4)], 267)
body += addtest("t_agg_nofill", [seq(f(1,T,2), ["ADDR", f"CAST 12 {p.pi}"], K(1,2), ["PTR_STRIDE 0","DEREF"], K(1,0), ["STORE 0 0"]), t_agg(0), ret(checksum(1))], [(1,T,4)], 267)
# INIT_CONST small: {1, 'x', {1,2,3}, {5, 6}} => 1+120+3+10+21+55+78 = 288
cagg = [f"C_AGG_BEGIN {T}", "C_SEL 0", "C_INT 1 1", "C_SEL 1", "C_INT 4 120", "C_SEL 2", f"C_AGG_BEGIN {A3}", "C_SEL 0", "C_INT 1 1", "C_SEL 1", "C_INT 1 2", "C_SEL 2", "C_INT 1 3", "C_AGG_END", "C_SEL 3", f"C_AGG_BEGIN {S}", "C_SEL 0", "C_INT 1 5", "C_SEL 1", "C_INT 1 6", "C_AGG_END", "C_AGG_END"]
body += addtest("t_init_const", pre_dirty(1) + [Lr(1) + cagg + ["INIT_CONST"], ret(checksum(1))], [(1,T,4)], 288)
# partial constant: unlisted parts are zero
cpart = [f"C_AGG_BEGIN {T}", "C_SEL 0", "C_INT 1 4", "C_SEL 3", f"C_AGG_BEGIN {S}", "C_SEL 1", "C_INT 1 2", "C_AGG_END", "C_AGG_END"]
body += addtest("t_init_const_partial", pre_dirty(1) + [Lr(1) + cpart + ["INIT_CONST"], ret(checksum(1))], [(1,T,4)], 4 + 26)
# INIT_ZERO
body += addtest("t_init_zero", pre_dirty(1) + [Lr(1) + ["INIT_ZERO"], ret(checksum(1))], [(1,T,4)], 0)
# large array: a[100] = {1, 2, [50..59]=4 (repeat), [99]=7}
big = [f"C_AGG_BEGIN {A100}", "C_SEL 0", "C_INT 1 1", "C_SEL 1", "C_INT 1 2", "C_REPEAT 50 10", "C_INT 1 4", "C_SEL 99", "C_INT 1 7", "C_AGG_END"]
def ai(l, i): return seq(Lr(l), ["ADDR", f"CAST 12 {p.pi}"], K(1,i), ["PTR_STRIDE 0","DEREF","LOAD 0"])
body += addtest("t_init_big", [Lr(1) + big + ["INIT_CONST"], ret(ADD(ADD(ADD(ADD(ai(1,0), MUL(ai(1,1),K(1,10))), MUL(ai(1,55),K(1,100))), MUL(ai(1,99),K(1,1000))), MUL(ai(1,49),K(1,10000))))], [(1,A100,16)], 1+20+400+7000+0)
body += addtest("t_init_big_zero", [seq(ai(1,5) and [] ), Lr(1) + ["INIT_ZERO"], ret(ADD(ai(1,0), ai(1,99)))], [(1,A100,16)], 0)
# MATERIALIZE_TEMP: mk(3,4).b
body += addtest("t_materialize", [ret(ADD(seq(CALL(mk,Tmk,[K(1,3),K(1,4)]), ["MATERIALIZE_TEMP 9 %d" % S, f"FIELD {S} 1", "LOAD 0"]), K(1,1)))], [], 5)
# a = (b = c) with records: the inner STORE pushes the stored record prvalue
body += addtest("t_chain_assign", pre_dirty(1) if False else [
  seq(f(3, S, 0), K(1, 1), ["STORE 0 0"]), seq(f(3, S, 1), K(1, 2), ["STORE 0 0"]),
  seq(Lr(1), Lr(2), Lr(3), ["LOAD 0", "STORE 0 2", "STORE 0 0"]),
  ret(ADD(MUL(seq(f(1, S, 0), ["LOAD 0"]), K(1, 10)), seq(f(1, S, 1), ["LOAD 0"])))], [(1, S, 4), (2, S, 4), (3, S, 4)], 12)
p.funcs = [body]
p.render(D+"expr-init.test", exp)
