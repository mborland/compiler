// S4 probe: the partial destruction of a throwing aggregate initialization (arrays of
// aggregates with array members), checked against g++.
#include <cstdio>
int c, d, throw_at;
struct A {
  A() { int i = c + 1; if (i == throw_at) throw i; ++c; }
  A(int i) { if (i == throw_at) throw i; ++c; }
  A(const A &) { throw 10; }
  ~A() { ++d; }
};
void try_idx(int i) {
  std::printf("trying %d\n", i);
  throw_at = i; c = d = 0;
  int t = 10;
  try {
    struct X { A e1[2], e2; } x2[3] = { { 1, 2, 3 }, { 4, 5, 6 } };
  } catch (int x) { t = x; }
  std::printf("t=%d c=%d d=%d\n", t, c, d);
}
int main() { for (int i = 1; i <= 10; ++i) try_idx(i); return 0; }
