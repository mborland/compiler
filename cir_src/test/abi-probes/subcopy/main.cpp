extern "C" int printf(const char *, ...);
#include "p.h"
static int bad = 0;
static void check(const char *what, long got, long want) {
  if (got != want) { ++bad; printf("MISMATCH %s: got %ld want %ld\n", what, got, want); }
}
#define CHECK(e, w) check(#e, (long)(e), (long)(w))
int main() {
  D d(1, 2, 3, 4); B b(9, 8);
  set_base(d, b);
  CHECK(d.a, 9); CHECK(d.c, 8); CHECK(d.d, 3); CHECK(d.e, 4);
  D d2(1, 2, 3, 4);
  set_base_val(d2, b);
  CHECK(d2.a, 9); CHECK(d2.c, 8); CHECK(d2.d, 3); CHECK(d2.e, 4);
  N n(1, 2, 3, 4);
  set_nua(n, b);
  CHECK(n.b.a, 9); CHECK(n.b.c, 8); CHECK(n.d, 3); CHECK(n.e, 4);
  { B g = get_base(d); CHECK(g.a, 9); CHECK(g.c, 8); }
  { B g = get_nua(n); CHECK(g.a, 9); CHECK(g.c, 8); }
  D d3(5, 6, 7, 8);
  assign_through_ref(base_ref(d3), b);
  CHECK(d3.a, 9); CHECK(d3.c, 8); CHECK(d3.d, 7); CHECK(d3.e, 8);
  DL dl(1, 2, 3, 4); L l(7, 6);
  set_base_l(dl, l);
  CHECK(dl.l, 7); CHECK(dl.c, 6); CHECK(dl.d, 3); CHECK(dl.m, 4);
  NL nl(1, 2, 3);
  set_nua_l(nl, l);
  CHECK(nl.l.l, 7); CHECK(nl.l.c, 6); CHECK(nl.d, 3);
  for (int i = 0; i < 6; ++i) CHECK(nl.e[i], i + 1);
  CHECK(sizeof(D), 8); CHECK(sizeof(N), 8); CHECK(sizeof(DL), 24); CHECK(sizeof(NL), 16);
  printf("subcopy: %s (%d mismatches)\n", bad == 0 ? "OK" : "BAD", bad);
  return bad != 0;
}
