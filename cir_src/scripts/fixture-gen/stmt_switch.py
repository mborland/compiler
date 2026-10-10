# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates test/cirgen/stmt-switch.test (the .test is the source of truth).
import os
def chk(fn, arg, exp):
    return f"""  CONST_INT 1 {arg}
  CALL {fn} 22 1 0
  CONST_INT 1 {exp}
  CMP 5
  IF_BEGIN 0
  CONST_INT 1 {chk.n}
  RETURN_STMT true
  IF_END
"""
def acc(l, v):
    return f"  LREF {l}\n  DUP\n  LOAD 0\n  CONST_INT 1 {v}\n  ADD 1\n  STORE 0 0\n"
def fn(gid, body):
    return f"  FUNC_BEGIN {gid} 0\n  PARAM 1 1 1 nil 0\n  LOCAL 2 1 nil 4 0\n  LREF 2\n  CONST_INT 1 0\n  STORE 0 0\n" + body + "  LREF 2\n  LOAD 0\n  RETURN_STMT true\n  FUNC_END 0\n"
sw = "  LREF 1\n  LOAD 0\n  SWITCH_BEGIN\n"
# f1: fallthrough, ranges, default in the middle
f1 = fn(40, sw + "  CASE_VALUE 1\n"+acc(2,1)+"  CASE_VALUE 2\n"+acc(2,10)+"  BREAK_STMT\n  CASE_RANGE 3 5\n"+acc(2,100)+"  BREAK_STMT\n  CASE_DEFAULT\n"+acc(2,1000)+"  CASE_VALUE 6\n"+acc(2,10000)+"  BREAK_STMT\n  SWITCH_END\n")
# f2: code before the first case (dead), no default, no match
f2 = fn(41, sw + acc(2,7)+"  CASE_VALUE 1\n"+acc(2,1)+"  SWITCH_END\n")
# f3: case labels inside a plain nested block
f3 = fn(42, sw + "  BLOCK_BEGIN\n  CASE_VALUE 1\n"+acc(2,1)+"  CASE_VALUE 2\n"+acc(2,2)+"  BLOCK_END\n  BREAK_STMT\n  CASE_VALUE 3\n"+acc(2,40)+"  SWITCH_END\n")
# f4: switch in a loop with break and continue; r accumulates
# for (i=0;i<5;++i) { switch(i){case 1: continue; case 3: break; default: r+=i;} r+=100; }
f4 = f"""  FUNC_BEGIN 43 0
  PARAM 1 1 1 nil 0
  LOCAL 2 1 nil 4 0
  LREF 2
  CONST_INT 1 0
  STORE 0 0
  FOR_BEGIN
  LOCAL 3 1 nil 4 0
  LREF 3
  CONST_INT 1 0
  STORE 0 0
  FOR_COND true
  LREF 3
  LOAD 0
  LREF 1
  LOAD 0
  CMP 0
  FOR_STEP true
  LREF 3
  INC_DEC 0 1
  DROP
  FOR_BODY
  LREF 3
  LOAD 0
  SWITCH_BEGIN
  CASE_VALUE 1
  CONTINUE_STMT
  CASE_VALUE 3
  BREAK_STMT
  CASE_DEFAULT
  LREF 2
  DUP
  LOAD 0
  LREF 3
  LOAD 0
  ADD 1
  STORE 0 0
  SWITCH_END
""" + acc(2,100) + """  FOR_END
  LREF 2
  LOAD 0
  RETURN_STMT true
  FUNC_END 0
"""
main = "  FUNC_BEGIN 44 0\n"
chk.n = 1
for (f,a,e) in [(40,1,11),(40,2,10),(40,4,100),(40,5,100),(40,6,10000),(40,9,11000),(41,1,1),(41,2,0),(42,1,3),(42,2,2),(42,3,40),(42,9,0),(43,5,0)]:
    main += chk(f,a,e); chk.n += 1
# f4 with arg 5: i=0:+0+100, 1:continue, 2:+2+100, 3:break+100, 4:+4+100 => 406
main = main.replace("CONST_INT 1 0\n  RETURN_STMT true\n  IF_END\n","X",0)
main += "  CONST_INT 1 0\n  RETURN_STMT true\n  FUNC_END 0\n"
main = main.replace("CALL 43 22 1 0\n  CONST_INT 1 0\n","CALL 43 22 1 0\n  CONST_INT 1 406\n")
hdr = """; Switch lowering through FlattenCFG (marker cases): fallthrough, range,
; default in the middle, code before the first case, case labels inside a
; nested block, break/continue in a switch inside a loop.
; RUN: cat %S/Inputs/header.txt %s > %t.txt
; RUN: %ecil-asm %t.txt -o %t.ecil
; RUN: %edg-cir --emit=cir %t.ecil | %FileCheck %s
; RUN: %edg-cir --emit=obj -o %t.o %t.ecil
; RUN: gcc %t.o -o %t.exe
; RUN: %t.exe

; CHECK: cir.switch
; CHECK: cir.case(range
; CHECK: cir.case(default

frame DECLS
  STR 10 "sw1"
  STR 11 "sw2"
  STR 12 "sw3"
  STR 13 "sw4"
  STR 14 "main"
  TY_FUNC 22 1 0 [1]
  TY_FUNC 21 1 0 []
  DECL_FUNC 40 22 0 1 10
  DECL_FUNC 41 22 0 1 11
  DECL_FUNC 42 22 0 1 12
  DECL_FUNC 43 22 0 1 13
  DECL_FUNC 44 21 0 9 14
frame REGION
"""
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen", "stmt-switch.test"),"w").write(hdr+f1+f2+f3+f4+main+"frame END\n  END auto 0\n")
