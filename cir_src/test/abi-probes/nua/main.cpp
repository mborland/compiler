extern "C" int printf(const char *, ...);
#include "p.h"
static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  CHECK(f_n1(N1(3)), 3); CHECK(f_n1(make_n1(4)), 4);
  { N1 r = pass_n1(N1(5)); CHECK(r.x, 5); }
  CHECK(f_n2(N2(6)), 6); CHECK(f_n2(make_n2(7)), 7);
  CHECK(f_n3(N3(8)), 8); CHECK(f_n3(make_n3(9)), 9);
  CHECK(f_n4(N4(1, 2, 3)), 10000 + 200 + 3); CHECK(f_n4(make_n4(4, 5, 6)), 40000 + 500 + 6);
  { N4 r = pass_n4(N4(7, 8, 9)); CHECK(r.b.a, 7); CHECK(r.b.c, 8); CHECK(r.d, 9); }
  CHECK(f_n5(N5(1, 2, 3, 4)), 10000 + 2000 + 300 + 4);
  CHECK(f_n5(make_n5(5, 6, 7, 8)), 50000 + 6000 + 700 + 8);
  { N5 r = pass_n5(N5(9, 1, 2, 3)); CHECK(r.l.l, 9); CHECK(r.l.c, 1); CHECK(r.d, 2); CHECK(r.e, 3); }
  CHECK(f_n6(N6(1, 2, 3)), 10000 + 200 + 3); CHECK(f_n6(make_n6(4, 5, 6)), 40000 + 500 + 6);
  CHECK(f_n7(N7(7.0)), 7); CHECK(f_n7(make_n7(8.0)), 8);
  CHECK(f_n8(N8(9)), 9); CHECK(f_n8(make_n8(10)), 10);
  CHECK(f_mix(1, N1(2), N4(3, 4, 5), N5(6, 7, 8, 9), N7(10.0), N8(11), 12),
        1 + 2 + (3 + 4 + 5) + (6 + 7 + 8 + 9) + 10 + 11 + 12);
  { N4 r = make_n4(1, 2, 3); CHECK(r.b.a, 1); CHECK(r.b.c, 2); CHECK(r.d, 3); }
  { N5 r = make_n5(1, 2, 3, 4); CHECK(r.l.l, 1); CHECK(r.l.c, 2); CHECK(r.d, 3); CHECK(r.e, 4); }
  { N7 r = make_n7(3.0); CHECK(r.f, 3); }
  CHECK(sizeof(N1), 4); CHECK(sizeof(N3), 4); CHECK(sizeof(N4), 8); CHECK(sizeof(N5), 16);
  printf("nua: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
