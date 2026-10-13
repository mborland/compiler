// S5 probe: new T[n]{...} with a run-time n: the first k elements from the
// list, the rest value-initialized (ctor or zeros), std::bad_array_new_length
// when n < k, partial destruction if a later element throws.
#include <cstdio>
#include <new>
int c, d;
struct S { int a; S() : a(-1) { ++c; if (a == 99) throw 1; std::printf("S()\n"); } S(int x) : a(x) { ++c; std::printf("S(%d)\n", x); } ~S() { ++d; std::printf("~S(%d)\n", a); } };
struct T { T() { std::puts("T()"); if (++c == 4) throw 5; } T(int) { ++c; std::puts("T(int)"); } ~T() { std::puts("~T"); } };
struct P { int x, y; };
int main() {
  volatile int n = 4;
  S *s = new S[n]{S(1), S(2)};
  std::printf("%d %d %d %d\n", s[0].a, s[1].a, s[2].a, s[3].a);
  delete[] s;
  P *p = new P[n]{{1, 2}, {3, 4}};
  std::printf("%d %d %d %d\n", p[0].x, p[1].y, p[2].x, p[3].y);
  delete[] p;
  int *q = new int[n]{7, 8, 9};
  std::printf("%d %d %d %d\n", q[0], q[1], q[2], q[3]);
  delete[] q;
  volatile int m = 1;
  try { int *r = new int[m]{1, 2}; std::printf("no %p\n", (void *)r); } catch (const std::bad_array_new_length &) { std::puts("bad_array_new_length"); }
  c = 0;
  try { T *t = new T[n]{T(1), T(2)}; delete[] t; } catch (int i) { std::printf("thrown %d\n", i); }
  return 0;
}
