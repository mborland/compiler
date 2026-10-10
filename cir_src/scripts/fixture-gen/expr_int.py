# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates a test/cirgen fixture (see lib.py); the generated .test is the
# source of truth, this only saves retyping when it needs to change.
from lib import *
p = Prog("""Integer operators, narrowing/widening casts and comparisons, run on the
box.  Expected values were computed by hand.""",
 ["cir.add nsw {{.*}} : !s32i", "cir.mul nsw", "cir.div", "cir.rem", "cir.shift(left", "cir.shift(right", "cir.or", "cir.xor", "cir.minus nsw", "cir.add {{%[0-9]+}}, {{%[0-9]+}} : !u32i", "cir.cmp lt", "cir.cast integral"])
S = lambda *a: list(a)
exp = []
def T(name, e, v, locals_=()):
    p.test(name, [ret(e)], locals_); exp.append(v)
T("t_addmul", SUB(MUL(ADD(K(1,7),K(1,5)),K(1,3)),K(1,4)), 32)
T("t_divrem", ADD(MUL(DIV(K(1,17),K(1,5)),K(1,10)), REM(K(1,17),K(1,5))), 32)   # 3*10+2
T("t_negdiv", ADD(MUL(DIV(K(1,-17),K(1,5)),K(1,100)), REM(K(1,-17),K(1,5))), -302)
T("t_shift_or_xor", XOR(OR(SHL(K(1,1),K(1,10)), SHR(K(1,240),K(1,4))), K(1,3)), 1036)
T("t_ashr", ADD(SHR(K(1,-8),K(1,1)), K(1,10)), 6)
T("t_unsigned_wrap", integral(1, REM(DIV(K(8,4294967295),K(8,3)),K(8,1000))), 765)
T("t_unsigned_wrap2", CMPI(4, ADD(K(8,4294967295),K(8,1),0), K(8,0)), 1)
T("t_lshr", integral(1, SHR(K(8,2147483648),K(1,31))), 1)
T("t_s64", integral(1, DIV(MUL(K(3,1099511627776),K(3,3)),K(3,1048576))), 3145728)
T("t_narrow", ADD(integral(1, integral(4, K(1,200))), integral(1, integral(5, K(1,300)))), -12)
T("t_widen", ADD(integral(1, K(5,255)), integral(1, K(4,-1))), 254)
T("t_cmp_kinds", ADD(ADD(ADD(CMPI(0,K(1,3),K(1,5)), MUL(CMPI(1,K(1,3),K(1,5)),K(1,2))), ADD(MUL(CMPI(2,K(1,3),K(1,5)),K(1,4)), MUL(CMPI(3,K(1,3),K(1,5)),K(1,8)))), ADD(MUL(CMPI(4,K(1,3),K(1,5)),K(1,16)), MUL(CMPI(5,K(1,3),K(1,5)),K(1,32)))), 35)
T("t_cmp_signedness", ADD(MUL(CMPI(0,K(1,-1),K(1,1)),K(1,10)), CMPI(0,K(8,4294967295),K(8,1))), 10)
T("t_unary", ADD(ADD(B("SUB",K(1,0),K(1,0),0) , seq(K(1,5),["NEG 1"])), ADD(seq(K(1,5),["BIT_NOT"]), ADD(cast(4,1,seq(K(1,5),["TO_BOOL","LOG_NOT"])), cast(4,1,seq(K(1,0),["TO_BOOL","LOG_NOT"]))))), -10)
import os
p.render(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen", "expr-int.test"), exp)
