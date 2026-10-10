# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
# ---- records: opaque, incomplete-then-completed, extract, const_ptr, casts
p = Prog("""TY_OPAQUE members (byte arrays that keep the layout), a record completed
after a pointer to it exists (linked list), EXTRACT, CONST_PTR, bitcast and
bool_to_float casts.""",
  ["!cir.array<!u8i x 16>", "cir.copy"])
exp = []
OP = p.ty("TY_OPAQUE @ 16 16")
R = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {R} 48 48 16 0 3", "  REC_FIELD 4 0 nil nil nil 0", f"  REC_FIELD {OP} 16 nil nil nil 0", "  REC_FIELD 1 32 nil nil nil 0"]
# linked list: declared, pointer taken, then completed
N = p.ty("TY_RECORD_DECL @ 0 nil"); PN = p.ty(f"TY_PTR @ {N}")
p.decls += [f"  TY_RECORD_BODY {N} 16 16 8 0 2", f"  REC_FIELD {PN} 0 nil nil nil 0", "  REC_FIELD 1 8 nil nil nil 0"]
S = p.ty("TY_RECORD_DECL @ 0 nil")
p.decls += [f"  TY_RECORD_BODY {S} 8 8 4 0 2", "  REC_FIELD 1 0 nil nil nil 0", "  REC_FIELD 1 4 nil nil nil 0"]
Tmk = p.func_type(S, [INT, INT]); mk = p.decl_func("mk", Tmk)
Tcp = p.func_type(INT, [PN]); 
def addtest(name, stmts, locals_, v):
    g = p.decl_func(name, p.func_type(INT, [])); p.tests.append(g); exp.append(v)
    return fbody(g, [], locals_, stmts)
def fld(l, rec, i): return [f"LREF {l}", f"FIELD {rec} {i}"]
body = []
body += fbody(mk, [(1,1,1),(2,1,1)], [(3,S,4)], [seq(fld(3,S,0),L(1),["STORE 0 0"]), seq(fld(3,S,1),L(2),["STORE 0 0"]), ret(Lr(3)+["LOAD 0"])])
# opaque: a record with opaque storage is copied as bytes, its other members work
body += addtest("t_opaque", [seq(fld(1,R,2),K(1,5),["STORE 0 0"]), seq(fld(1,R,0),K(4,65),["STORE 0 0"]), seq(Lr(2),Lr(1),["LOAD 0","STORE 0 0"]), ret(ADD(seq(fld(2,R,2),["LOAD 0"]), integral(1,seq(fld(2,R,0),["LOAD 0"]))))], [(1,R,16),(2,R,16)], 70)
# list: n3 -> n2 -> n1 -> null; sum values by chasing pointers
def nf(l, i): return fld(l, N, i)
body += addtest("t_list", [
  seq(nf(1,0),[f"CONST_NULL {PN}"],["STORE 0 0"]), seq(nf(1,1),K(1,1),["STORE 0 0"]),
  seq(nf(2,0),Lr(1),["ADDR"],["STORE 0 0"]), seq(nf(2,1),K(1,20),["STORE 0 0"]),
  seq(nf(3,0),Lr(2),["ADDR"],["STORE 0 0"]), seq(nf(3,1),K(1,300),["STORE 0 0"]),
  assign(4, Lr(3)+["ADDR"]), assign(5, K(1,0)),
  seq(["WHILE_BEGIN"], L(4), [f"CAST 3 15"], ["WHILE_BODY"],
      Lr(5), ["DUP","LOAD 0"], L(4), ["DEREF", f"FIELD {N} 1", "LOAD 0", "ADD 1", "STORE 0 0"],
      Lr(4), L(4), ["DEREF", f"FIELD {N} 0", "LOAD 0", "STORE 0 0", "WHILE_END"]),
  ret(L(5))], [(1,N,8),(2,N,8),(3,N,8),(4,PN,8),(5,1,4)], 321)
body += addtest("t_extract", [ret(ADD(seq(CALL(mk,Tmk,[K(1,3),K(1,4)]),[f"EXTRACT {S} 1"]), MUL(seq(CALL(mk,Tmk,[K(1,3),K(1,4)]),[f"EXTRACT {S} 0"]),K(1,10))))], [], 34)
body += addtest("t_const_ptr", [ret(cast(0,1,cast(10,S64,[f"CONST_PTR {IPTR} 4096"])))] , [], 4096)
body += addtest("t_bitcast", [assign(1, K(1,258)), ret(ADD(integral(1,seq(Lr(1),["ADDR",f"CAST 11 {CPTR}","DEREF","LOAD 0"])), seq(Lr(1),["ADDR",f"CAST 11 {CPTR}",f"CAST 11 {IPTR}","DEREF","LOAD 0"])))], [(1,1,4)], 2+258)
body += addtest("t_bool_to_float", [ret(cast(7,1,ADD(cast(5,DBL,CMPB(0,K(1,1),K(1,2))), KF(DBL,2.5),0)))], [], 3)
p.funcs = [body]
p.render(D+"records.test", exp)
