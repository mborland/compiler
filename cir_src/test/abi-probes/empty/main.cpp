extern "C" int printf(const char *, ...);
#include "p.h"
static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
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
  for (int i = 0; i < layout_count(); ++i) check("layout item", layout_item(i), table_value(i));
  CHECK(f_ebe(EBE(3)), 3); CHECK(f_ebe(make_ebe(4)), 4);
  CHECK(f_zb(ZB(5)), 5); CHECK(f_zb(make_zb(6)), 6);
  CHECK(f_ne2(NE2(7)), 7); CHECK(f_ne2(make_ne2(8)), 8);
  { EBE r = make_ebe(9); CHECK(r.x, 9); }
  { ZB r = make_zb(10); CHECK(r.x, 10); }
  { NE2 r = make_ne2(11); CHECK(r.x, 11); }
  printf("empty: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
