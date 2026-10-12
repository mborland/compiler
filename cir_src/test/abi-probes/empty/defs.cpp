#include "p.h"
int f_e(E, int x) { return x; }
int f_e_mid(int a, E, int b) { return a * 10 + b; }
int f_e2(E2, E3, int x) { return x; }
int f_many(int a, int b, int c, int d, int e, E, int f, E, int g, int h) {
  return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}
int f_many_d(double a, double b, double c, double d, double e, double f,
             double g, E, double h, double i) {
  return (int)(a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9);
}
E make_e() { return E(); }
E2 make_e2() { return E2(); }
E pass_e(E e) { return e; }
int f_se(SE s) { return s.x; }
SE make_se(int v) { return SE(v); }
int f_es(ES s) { return s.x; }
ES make_es(int v) { return ES(v); }
int f_see(SEE, int x) { return x; }
SEE make_see() { return SEE(); }
int f_de(DE s) { return s.x; }
DE make_de(int v) { return DE(v); }
int f_el(EL s) { return (int)s.l; }
EL make_el(long v) { return EL(v); }
int f_ef(EF s) { return (int)s.f; }
EF make_ef(double v) { return EF(v); }
int f_ea(EA, int x) { return x; }
int f_mix(int a, E, SE s, ES t, DE u, int b) {
  return a + s.x * 10 + t.x * 100 + u.x * 1000 + b * 10000;
}
int f_ebe(EBE s) { return s.x; }
EBE make_ebe(int v) { return EBE(v); }
int f_zb(ZB s) { return s.x; }
ZB make_zb(int v) { return ZB(v); }
int f_ne2(NE2 s) { return (int)s.x; }
NE2 make_ne2(long v) { return NE2(v); }
#define OFFSET(T, m) ((long)((char *)&((T *)0)->m - (char *)0))
static long table_value(int i) {
  switch (i) {
  case 0: return sizeof(SE); case 1: return OFFSET(SE, x);
  case 2: return sizeof(ES); case 3: return OFFSET(ES, x);
  case 4: return sizeof(SEE); case 5: return sizeof(DE); case 6: return OFFSET(DE, x);
  case 7: return sizeof(EL); case 8: return OFFSET(EL, l);
  case 9: return sizeof(EA); case 10: return sizeof(EBE); case 11: return OFFSET(EBE, e); case 12: return OFFSET(EBE, x);
  case 13: return sizeof(ZB); case 14: return OFFSET(ZB, x);
  case 15: return sizeof(NE2); case 16: return OFFSET(NE2, x);
  case 17: return OFFSET(NE2, a); case 18: return OFFSET(NE2, b); case 19: return OFFSET(NE2, c);
  }
  return -1;
}
long layout_item(int i) { return table_value(i); }
int layout_count() { return 20; }
