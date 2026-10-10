# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Regenerates test/cirgen/expr-builtins.test (minor-5 builtins).
import os
from lib import *
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "cirgen") + "/"
p = Prog("""Minor-5 builtins: bswap, clz/ctz, popcount, parity, ffs, clrsb,
add/sub/mul_overflow (mixed integer types, wrapped result), alloca.""",
  ["cir.byte_swap", "cir.clz", "cir.ctz", "cir.popcount", "cir.parity", "cir.ffs", "cir.clrsb", "cir.add.overflow", "cir.sub.overflow", "cir.mul.overflow", "cir.alloca", "cir.signbit", "cir.is_fp_class", "cir.cmp one", "cir.cmp uno"])
exp = []
UCP = p.ty("TY_PTR @ 5")
def T(name, stmts, v, locals_=()):
    p.test(name, stmts, locals_); exp.append(v)
def bi(i, args, t): return seq(*args) + [f"BUILTIN {i} {len(args)} {t}"]
def cmp_eq(a, v): return CMPI(4, a, v)
T("b_bswap", [ret(cmp_eq(bi(5, [K(8, 0x11223344)], UINT), K(8, 0x44332211)))], 1)
T("b_clz_ctz", [ret(ADD(bi(6, [K(8, 1)], INT), MUL(bi(7, [K(8, 8)], INT), K(1, 100))))], 331)
T("b_popcount_parity", [ret(ADD(bi(8, [K(8, 0xF0F0)], INT), MUL(bi(9, [K(8, 7)], INT), K(1, 100))))], 108)
T("b_ffs_clrsb", [ret(ADD(ADD(bi(10, [K(1, 8)], INT), bi(10, [K(1, 0)], INT)), MUL(bi(11, [K(1, 1)], INT), K(1, 100))))], 3004)
# overflow: int wraps; result pointer to a local
T("b_add_overflow", [ret(ADD(bi(12, [K(1, 2147483647), K(1, 1), Lr(1)+["ADDR"]], BOOL) and cast(4, 1, bi(12, [K(1, 2147483647), K(1, 1), Lr(1)+["ADDR"]], BOOL)), cast(4, 1, cmp_eq(L(1), K(1, -2147483648)) if False else CMPB(4, L(1), K(1, -2147483648)))))], 2, [(1, 1, 4)])
T("b_mixed_add", [seq(bi(12, [K(5, 200), K(5, 100), Lr(1)+["ADDR", f"CAST 11 {UCP}"]], BOOL), ["DROP"]), ret(integral(1, L(1)))], 44, [(1, UCHAR, 1)])
T("b_sub_mul", [ret(ADD(cast(4, 1, bi(13, [K(8, 3), K(8, 5), Lr(1)+["ADDR"]], BOOL)), MUL(cast(4, 1, bi(14, [K(1, 1048576), K(1, 1048576), Lr(2)+["ADDR"]], BOOL)), K(1, 10))))], 11, [(1, UINT, 4), (2, INT, 4)])
T("b_no_overflow", [ret(ADD(cast(4, 1, bi(12, [K(1, 2), K(1, 3), Lr(1)+["ADDR"]], BOOL)), L(1)))], 5, [(1, 1, 4)])
# alloca: 40 bytes of function-lifetime storage
T("b_alloca", [assign(1, bi(4, [K(U64, 40)], VPTR)), seq(L(1), [f"CAST 11 {IPTR}", "DEREF"], K(1, 7), ["STORE 0 0"]), ret(seq(L(1), [f"CAST 11 {IPTR}", "DEREF", "LOAD 0"]))], 7, [(1, VPTR, 8)])
# minor 6: floating-point classification and unordered compares
nan = lambda: DIV(KF(DBL, 0.0), KF(DBL, 0.0), 0)
T("b_signbit", [ret(ADD(cast(4, 1, bi(16, [seq(KF(DBL, -0.0))], BOOL)), cast(4, 1, bi(16, [KF(DBL, 2.0)], BOOL))))], 1)
T("b_is_fp_class", [ret(ADD(ADD(cast(4, 1, bi(15, [nan(), K(1, 3)], BOOL)), MUL(cast(4, 1, bi(15, [DIV(KF(DBL, 1.0), KF(DBL, 0.0), 0), K(1, 0x204)], BOOL)), K(1, 10))), MUL(cast(4, 1, bi(15, [KF(DBL, 1.5), K(1, 0x3FF & ~0x300 | 0x100)], BOOL)), K(1, 100))))], 111)
T("b_cmp_one_uno", [ret(ADD(ADD(CMPI(7, nan(), KF(DBL, 1.0)), MUL(CMPI(6, KF(DBL, 1.0), KF(DBL, 2.0)), K(1, 10))), MUL(CMPI(6, nan(), KF(DBL, 1.0)), K(1, 100))))], 11)
p.render(D + "expr-builtins.test", exp)
