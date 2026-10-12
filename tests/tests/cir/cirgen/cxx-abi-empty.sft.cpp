//type:rp
//options:--c++20
//require:BACK_END_TYPE 3
// Empty classes passed and returned by value, alone, as members, bases and
// [[no_unique_address]] members (no register or stack slot, size and offsets). The
// two-translation-unit version (cir_src/test/abi-probes, box_abi_probe.sh) is the ABI proof.
extern "C" int printf(const char *, ...);
// Empty classes passed and returned by value, alone and as members and bases.
struct E {};
struct E2 : E {};
struct E3 : E2 {};
struct SE { E e; int x; SE(int v) : x(v) {} };                  // 8
struct ES { int x; E e; ES(int v) : x(v) {} };                   // 8
struct SEE { E a; E b; SEE() {} };                              // 2
struct DE : E { int x; DE(int v) : x(v) {} };                   // 4
struct EL { E e; long l; EL(long v) : l(v) {} };                // 16
struct EF { E e; double f; EF(double v) : f(v) {} };            // 16
struct EA { E e[3]; };                                          // 3

struct EBE : E { E e; int x; EBE(int v) : x(v) {} };              // base E at 0, member e at 1
struct ZB : E, E3 { int x; ZB(int v) : x(v) {} };                  // E3 shares E's address: pushed to 1
struct NE2 { [[no_unique_address]] E a; [[no_unique_address]] E b; [[no_unique_address]] E c; long x; NE2(long v) : x(v) {} };
long layout_item(int index);       // sizeof/offsetof facts, computed by the TU that defines them
int layout_count();
int f_ebe(EBE s);      EBE make_ebe(int);
int f_zb(ZB s);        ZB make_zb(int);
int f_ne2(NE2 s);      NE2 make_ne2(long);
int f_e(E e, int x);
int f_e_mid(int a, E e, int b);
int f_e2(E2 e, E3 e3, int x);
int f_many(int a, int b, int c, int d, int e, E x, int f, E y, int g, int h);
int f_many_d(double a, double b, double c, double d, double e, double f,
             double g, E x, double h, double i);
E make_e();
E2 make_e2();
E pass_e(E e);
int f_se(SE s);       SE make_se(int);
int f_es(ES s);       ES make_es(int);
int f_see(SEE s, int x);  SEE make_see();
int f_de(DE s);       DE make_de(int);
int f_el(EL s);       EL make_el(long);
int f_ef(EF s);       EF make_ef(double);
int f_ea(EA s, int x);
int f_mix(int a, E e, SE s, ES t, DE u, int b);

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

static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
/* sizeof and offsetof of the first-TU table above, as g++ 15 and the pin's
   clang 24 compute them (they agree): this TU's layout must equal them. */
static const long expected_layout[20] = {
  8, 4, 8, 0, 2, 4, 0, 16, 8, 3, 8, 1, 4, 4, 0, 8, 0, 0, 1, 2
};
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  E e; E2 e2; E3 e3; SEE see; EA ea;
  CHECK(f_e(e, 5), 5); CHECK(f_e_mid(4, e, 6), 46); CHECK(f_e2(e2, e3, 9), 9);
  CHECK(f_many(1, 2, 3, 4, 5, e, 6, e, 7, 8),
        1 + 4 + 9 + 16 + 25 + 36 + 49 + 64);
  CHECK(f_many_d(1, 2, 3, 4, 5, 6, 7, e, 8, 9),
        1 + 4 + 9 + 16 + 25 + 36 + 49 + 64 + 81);
  (void)make_e(); (void)make_e2(); (void)pass_e(e);
  CHECK(f_se(SE(3)), 3); CHECK(f_se(make_se(4)), 4);
  CHECK(f_es(ES(5)), 5); CHECK(f_es(make_es(6)), 6);
  CHECK(f_see(see, 7), 7); CHECK(f_see(make_see(), 8), 8);
  CHECK(f_de(DE(9)), 9); CHECK(f_de(make_de(10)), 10);
  CHECK(f_el(EL(11)), 11); CHECK(f_el(make_el(12)), 12);
  CHECK(f_ef(EF(13.0)), 13); CHECK(f_ef(make_ef(14.0)), 14);
  CHECK(f_ea(ea, 15), 15);
  CHECK(f_mix(1, e, SE(2), ES(3), DE(4), 5), 1 + 20 + 300 + 4000 + 50000);
  { SE r = make_se(21); CHECK(r.x, 21); }
  { ES r = make_es(22); CHECK(r.x, 22); }
  { DE r = make_de(23); CHECK(r.x, 23); }
  { EL r = make_el(24); CHECK(r.l, 24); }
  { EF r = make_ef(25.0); CHECK(r.f, 25); }
  CHECK(layout_count(), 20);
  for (int i = 0; i < layout_count(); ++i) check("layout item", layout_item(i), expected_layout[i]);
  CHECK(f_ebe(EBE(3)), 3); CHECK(f_ebe(make_ebe(4)), 4);
  CHECK(f_zb(ZB(5)), 5); CHECK(f_zb(make_zb(6)), 6);
  CHECK(f_ne2(NE2(7)), 7); CHECK(f_ne2(make_ne2(8)), 8);
  { EBE r = make_ebe(9); CHECK(r.x, 9); }
  { ZB r = make_zb(10); CHECK(r.x, 10); }
  { NE2 r = make_ne2(11); CHECK(r.x, 11); }
  printf("empty: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
