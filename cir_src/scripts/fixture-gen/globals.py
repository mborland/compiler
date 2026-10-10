# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
LL=[]
def w(*a): LL.extend(a)
sid={}
def S_(t):
    sid.setdefault(t, 10+len(sid)); return sid[t]
types=[]
gid=[20]
def T(line):
    gid[0]+=1; types.append("  "+line.replace("@",str(gid[0]))); return gid[0]
S = T("TY_RECORD_DECL @ 0 nil"); 
types += [f"  TY_RECORD_BODY {S} 12 12 4 0 3", "  REC_FIELD 4 0 nil nil nil 0","  REC_FIELD 1 4 nil nil nil 0","  REC_FIELD 6 8 nil nil nil 0"]
B = T("TY_RECORD_DECL @ 0 nil")
types += [f"  TY_RECORD_BODY {B} 16 16 8 0 4", "  REC_FIELD 1 0 0 3 nil 4","  REC_FIELD 1 0 3 5 nil 4","  REC_FIELD 8 1 0 20 nil 0","  REC_FIELD 3 8 0 40 nil 4"]
PB = T("TY_RECORD_DECL @ 0 nil")
types += [f"  TY_RECORD_BODY {PB} 6 6 1 1 4", "  REC_FIELD 4 0 nil nil nil 0","  REC_FIELD 1 1 0 4 nil 4","  REC_FIELD 1 1 4 20 nil 4","  REC_FIELD 6 4 nil nil nil 0"]
A8 = T("TY_ARRAY @ 4 8 0"); U = T("TY_RECORD_DECL @ 1 nil")
types += [f"  TY_RECORD_BODY {U} 8 8 4 0 2", "  REC_FIELD 1 0 nil nil nil 0", f"  REC_FIELD {A8} 0 nil nil nil 0"]
AS2 = T(f"TY_ARRAY @ {S} 2 0"); N = T("TY_RECORD_DECL @ 0 nil")
types += [f"  TY_RECORD_BODY {N} 28 28 4 0 2", f"  REC_FIELD {AS2} 0 nil nil nil 0", "  REC_FIELD 4 24 nil nil nil 0"]
A6 = T("TY_ARRAY @ 4 6 0"); A4 = T("TY_ARRAY @ 1 4 0"); A100 = T("TY_ARRAY @ 1 100 0"); A10 = T("TY_ARRAY @ 4 10 0")
TF = T("TY_FUNC @ 1 0 [1]"); PF = T(f"TY_PTR @ {TF}"); AF2 = T(f"TY_ARRAY @ {PF} 2 0")
types.append("  TY_FUNC 99 1 0 [1]") if False else None
# twice
twice = 100
Dw = []
def var(name, t, init, linkage=0, flags=1, align=0):
    g = var.n; var.n += 1
    Dw.append(f"  DECL_VAR {g} {t} {linkage} {flags} {align} {S_(name)}")
    if init is not None:
        Dw.extend("  "+x for x in init)
        Dw.append(f"  VAR_INIT {g}")
    return g
var.n = 200
g_i = var("g_i", 1, ["C_INT 1 5"])
g_str = var("g_str", A6, ['C_STRING %d bin8:"hello"' % A6])
strlit = var(".str", A6, ['C_STRING %d bin8:"world"' % A6], linkage=1, flags=1|2|8)
g_p = var("g_p", 13, [f"C_ADDR 13 {strlit} 0"])
g_arr = var("g_arr", A4, [f"C_AGG_BEGIN {A4}", "C_SEL 0","C_INT 1 1","C_SEL 1","C_INT 1 2","C_SEL 2","C_INT 1 3","C_SEL 3","C_INT 1 4","C_AGG_END"])
g_ap = var("g_ap", 14, [f"C_ADDR 14 {g_arr} 8"])
g_s = var("g_s", S, [f"C_AGG_BEGIN {S}","C_SEL 0","C_INT 4 1","C_SEL 1","C_INT 1 2","C_SEL 2","C_INT 6 3","C_AGG_END"])
g_b = var("g_b", B, [f"C_AGG_BEGIN {B}","C_SEL 0","C_INT 1 -2","C_SEL 1","C_INT 1 7","C_SEL 2","C_INT 8 12345","C_SEL 3","C_INT 3 123456789012","C_AGG_END"])
g_pb = var("g_pb", PB, [f"C_AGG_BEGIN {PB}","C_SEL 0","C_INT 4 9","C_SEL 1","C_INT 1 -3","C_SEL 2","C_INT 1 54321","C_SEL 3","C_INT 6 77","C_AGG_END"])
g_u = var("g_u", U, [f"C_AGG_BEGIN {U}","C_SEL 1",f"C_AGG_BEGIN {A8}","C_SEL 0","C_INT 4 1","C_SEL 7","C_INT 4 8","C_AGG_END","C_AGG_END"])
g_d = var("g_d", 10, [f"C_FLOAT 10 {fl(DBL,1.5)}"])
g_f = var("g_f", 9, [f"C_FLOAT 9 {fl(FLT,2.5)}"])
g_ld = var("g_ld", 16, ["C_FLOAT 16 fbits:10:0x4000d000000000000000"])
g_fp = var("g_fp", PF, [f"C_ADDR {PF} {twice} 0"])
g_t = var("g_t", 1, None, flags=0x21)
g_rep = var("g_rep", A100, [f"C_AGG_BEGIN {A100}","C_REPEAT 10 10","C_INT 1 9","C_SEL 99","C_INT 1 7","C_AGG_END"])
sa = lambda a,b,c: [f"C_AGG_BEGIN {S}"] + a + ["C_AGG_END"]
g_n = var("g_n", N, [f"C_AGG_BEGIN {N}","C_SEL 0",f"C_AGG_BEGIN {AS2}","C_SEL 0",f"C_AGG_BEGIN {S}","C_SEL 0","C_INT 4 1","C_AGG_END","C_SEL 1",f"C_AGG_BEGIN {S}","C_SEL 1","C_INT 1 6","C_SEL 2","C_INT 6 7","C_AGG_END","C_AGG_END","C_SEL 1","C_INT 4 122","C_AGG_END"])
g_s2 = var("g_s2", A10, ['C_STRING %d bin8:"ab"' % A10])
g_null = var("g_null", 14, ["C_NULL 14"])
g_const = var("g_const", 1, ["C_INT 1 42"], flags=3)
g_weak = var("g_weak", 1, ["C_INT 1 3"], linkage=5)
g_ip = var("g_ip", 14, ["C_PTR 14 4096"])
g_farr = var("g_farr", AF2, [f"C_AGG_BEGIN {AF2}","C_SEL 0",f"C_ADDR {PF} {twice} 0","C_SEL 1",f"C_NULL {PF}","C_AGG_END"])
out = ["; Global definitions and constant initializers (C_*): scalars, strings, string",
 "; literals, addresses (global_view offsets), records with padding, bit-field",
 "; packing, packed records, unions, nested aggregates, repeats, long double,",
 "; tentative definitions, weak/const.  gcc compiles Inputs/globals_check.c",
 "; against the object and checks every value and the layout.",
 "; RUN: cat %S/Inputs/header.txt %s > %t.txt",
 "; RUN: %ecil-asm %t.txt -o %t.ecil",
 "; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s",
 "; RUN: %edg-cir --emit=obj -o %t.o %t.ecil",
 "; RUN: gcc %S/Inputs/globals_check.c %t.o -o %t.exe",
 "; RUN: %t.exe",
 "",
 "; CHECK-DAG: cir.global external dso_local @g_i = #cir.int<5> : !s32i",
 "; CHECK-DAG: @g_str = #cir.const_array<\"hello\" : !cir.array<!s8i x 5>, trailing_zeros> : !cir.array<!s8i x 6>",
 "; CHECK-DAG: cir.global \"private\" constant internal {{.*}}@\".str\"",
 "; CHECK-DAG: #cir.global_offset<@g_arr, 8>",
 "; CHECK-DAG: @g_t = #cir.int<0>",
 "; CHECK-DAG: cir.global weak",
 "; CHECK-DAG: constant external {{.*}}@g_const",
 "", "frame DECLS"]
out += [f'  STR {v} "{k}"' for k,v in sid.items()]
out += types
out += ["  TY_FUNC 98 1 0 [1]"] if False else []
out += [f'  STR 9 "twice"', f"  DECL_FUNC {twice} {TF} 0 1 9"]
out += ["frame REGION"] + fbody(twice, [(1,1,1)], [], [ret(MUL(L(1),K(1,2)))])
out += ["frame GLOBALS"] + Dw
out += ["frame END", "  END auto 0", ""]
open(D+"globals.test","w").write("\n".join(out))
