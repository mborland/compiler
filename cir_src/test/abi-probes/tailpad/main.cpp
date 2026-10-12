extern "C" int printf(const char *, ...);
#include "p.h"
static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  B b(7, 3); D d(7, 3, 5); L l(9, 4); LD ld(9, 4, 6, 12);
  FD fd(2.0, 4); FDD fdd(2.0, 4, 6); FF ff(2.0f, 4); FFD ffd(2.0f, 4, 6);
  DD dd(7, 3, 5, 9); M1 m1(7, 3); M2 m2(7, 3); TT tt(9, 4);
  CHECK(sum_b(b), 703); CHECK(sum_b(make_b(8, 2)), 802);
  { B r = pass_b(b); CHECK(r.a, 7); CHECK(r.c, 3); }
  CHECK(sum_d(d), 70305); CHECK(sum_d(make_d(8, 2, 1)), 80201);
  { D r = pass_d(d); CHECK(r.a, 7); CHECK(r.c, 3); CHECK(r.d, 5); }
  { D r = make_d(1, 2, 3); CHECK(r.a, 1); CHECK(r.c, 2); CHECK(r.d, 3); }
  CHECK(sum_l(l), 904); CHECK(sum_l(make_l(11, 5)), 1105);
  CHECK(sum_ld(ld), 94000 + 600 + 12); CHECK(sum_ld(make_ld(2, 3, 4, 5)), 20000 + 3000 + 400 + 5);
  { LD r = pass_ld(ld); CHECK(r.l, 9); CHECK(r.c, 4); CHECK(r.d, 6); CHECK(r.e, 12); }
  CHECK(sum_fd(fd), 204); CHECK(sum_fd(make_fd(3.0, 7)), 307);
  CHECK(sum_fdd(fdd), 20000 + 400 + 6); CHECK(sum_fdd(make_fdd(3.0, 7, 2)), 30000 + 700 + 2);
  { FDD r = pass_fdd(fdd); CHECK(r.x, 2); CHECK(r.c, 4); CHECK(r.d, 6); }
  CHECK(sum_ff(ff), 204); CHECK(sum_ff(make_ff(3.0f, 7)), 307);
  CHECK(sum_ffd(ffd), 20000 + 400 + 6); CHECK(sum_ffd(make_ffd(3.0f, 7, 2)), 30000 + 700 + 2);
  { FFD r = pass_ffd(ffd); CHECK(r.f, 2); CHECK(r.c, 4); CHECK(r.d, 6); }
  CHECK(sum_dd(dd), 7000000 + 30000 + 500 + 9);
  CHECK(sum_dd(make_dd(1, 2, 3, 4)), 1000000 + 20000 + 300 + 4);
  { DD r = pass_dd(dd); CHECK(r.a, 7); CHECK(r.c, 3); CHECK(r.d, 5); CHECK(r.e, 9); }
  CHECK(sum_m1(m1), 703); CHECK(sum_m1(make_m1(5, 1)), 501);
  CHECK(sum_m2(m2), 70000 + 300 + 80 + 4); CHECK(sum_m2(make_m2(1, 2)), 10000 + 200 + 20 + 3);
  CHECK(sum_tt(tt), 904 + (4 + 5 + 6 + 7 + 8 + 9 + 10)); CHECK(sum_tt(make_tt(1, 2)), 102 + (2 + 3 + 4 + 5 + 6 + 7 + 8));
  CHECK(many(1, d, 2, ld, 3, fdd, 4, ffd, 5, dd, 6),
        1 + (7 + 3 + 5) + 2 + (9 + 4 + 6 + 12) + 3 + (2 + 4 + 6) + 4 + (2 + 4 + 6) + 5 + (7 + 3 + 5 + 9) + 6);
  { SD a(3, 4, 5); CHECK(sum_sd(a), 30405); CHECK(sum_sd(make_sd(1, 2, 3)), 10203);
    SD r = pass_sd(a); CHECK(r.s, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { XD a(3.0L, 4, 5); CHECK(sum_xd(a), 30000 + 405); CHECK(sum_xd(make_xd(1.0L, 2, 3)), 10000 + 203);
    XD r = pass_xd(a); CHECK(r.x, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { AD a(10, 4); CHECK(sum_ad(a), 100000 + 11000 + 1200 + 40 + 6); CHECK(sum_ad(make_ad(1, 2)), 10000 + 2000 + 300 + 20 + 6);
    AD r = pass_ad(a); CHECK(r.x[0], 10); CHECK(r.x[2], 12); CHECK(r.c, 4); CHECK(r.d[0], 1); CHECK(r.d[2], 3); }
  { QD a(3, 4, 5); CHECK(sum_qd(a), 30000 + 405); CHECK(sum_qd(make_qd(1, 2, 3)), 10000 + 203);
    QD r = pass_qd(a); CHECK(r.q, 3); CHECK(r.c, 4); CHECK(r.d, 5); }
  { HD a(1, 2, 3); CHECK(sum_hd(a), 10000 + 200 + 3); CHECK(sum_hd(make_hd(4, 5, 6)), 40000 + 500 + 6);
    HD r = pass_hd(a); CHECK(r.a, 1); CHECK(r.b, 2); CHECK(r.d, 3); }
  CHECK(sizeof(SD), 4); CHECK(sizeof(XD), 32); CHECK(sizeof(AD), 16); CHECK(sizeof(QD), 32); CHECK(sizeof(HD), 3);
  printf("tailpad: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
