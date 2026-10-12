#include "p.h"
int sum_b(B v) { return v.a * 100 + v.c; }
B make_b(int a, char c) { return B(a, c); }
B pass_b(B v) { return v; }
int sum_d(D v) { return v.a * 10000 + v.c * 100 + v.d; }
D make_d(int a, char c, char d) { return D(a, c, d); }
D pass_d(D v) { return v; }
int sum_l(L v) { return (int)v.l * 100 + v.c; }
L make_l(long l, char c) { return L(l, c); }
int sum_ld(LD v) { return (int)v.l * 10000 + v.c * 1000 + v.d * 100 + v.e; }
LD make_ld(long l, char c, char d, short e) { return LD(l, c, d, e); }
LD pass_ld(LD v) { return v; }
int sum_fd(FD v) { return (int)v.x * 100 + v.c; }
FD make_fd(double x, char c) { return FD(x, c); }
int sum_fdd(FDD v) { return (int)v.x * 10000 + v.c * 100 + v.d; }
FDD make_fdd(double x, char c, char d) { return FDD(x, c, d); }
FDD pass_fdd(FDD v) { return v; }
int sum_ff(FF v) { return (int)v.f * 100 + v.c; }
FF make_ff(float f, char c) { return FF(f, c); }
int sum_ffd(FFD v) { return (int)v.f * 10000 + v.c * 100 + v.d; }
FFD make_ffd(float f, char c, char d) { return FFD(f, c, d); }
FFD pass_ffd(FFD v) { return v; }
int sum_dd(DD v) { return v.a * 1000000 + v.c * 10000 + v.d * 100 + v.e; }
DD make_dd(int a, char c, char d, char e) { return DD(a, c, d, e); }
DD pass_dd(DD v) { return v; }
int sum_m1(M1 v) { return v.a * 100 + v.c; }
M1 make_m1(int a, char c) { return M1(a, c); }
int sum_m2(M2 v) { return v.a * 10000 + v.c * 100 + v.inner.a * 10 + v.inner.c; }
M2 make_m2(int a, char c) { return M2(a, c); }
int sum_tt(TT v) {
  int s = (int)v.l * 100 + v.c;
  for (int i = 0; i < 7; ++i) s += v.d[i];
  return s;
}
TT make_tt(long l, char c) { return TT(l, c); }
int many(int a, D v, int b, LD w, int c, FDD x, int d, FFD y, int e, DD z,
         int f) {
  return a + v.a + v.c + v.d + b + (int)w.l + w.c + w.d + w.e + c + (int)x.x +
         x.c + x.d + d + (int)y.f + y.c + y.d + e + z.a + z.c + z.d + z.e + f;
}
int sum_sd(SD v) { return v.s * 10000 + v.c * 100 + v.d; }
SD make_sd(short s, char c, char d) { return SD(s, c, d); }
SD pass_sd(SD v) { return v; }
int sum_xd(XD v) { return (int)v.x * 10000 + v.c * 100 + v.d; }
XD make_xd(long double x, char c, char d) { return XD(x, c, d); }
XD pass_xd(XD v) { return v; }
int sum_ad(AD v) { return v.x[0] * 10000 + v.x[1] * 1000 + v.x[2] * 100 + v.c * 10 + v.d[0] + v.d[1] + v.d[2]; }
AD make_ad(int a, char b) { return AD(a, b); }
AD pass_ad(AD v) { return v; }
int sum_qd(QD v) { return (int)v.q * 10000 + v.c * 100 + v.d; }
QD make_qd(long a, char c, char d) { return QD(a, c, d); }
QD pass_qd(QD v) { return v; }
int sum_hd(HD v) { return v.a * 10000 + v.b * 100 + v.d; }
HD make_hd(char a, char b, char c) { return HD(a, b, c); }
HD pass_hd(HD v) { return v; }
