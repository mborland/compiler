extern "C" int printf(const char *, ...);
template <class F> int apply(F f, int x) { return f(x); }
struct W { int base; int k = [] { return 7; }(); int (*fp)(int) = [](int x) { return x + 1; };
  int run(int x) const { auto l = [this, x](int y) { return y + base + x; }; return l(1); } };
int g_init = [] { return 5; }();
int (*g_fp)(int) = [](int x) { return x * 3; };
auto make_counter(int start) { return [start]() mutable { return start++; }; }
struct Big { int a[4]; };
auto big = [](Big b) { return b.a[0] + b.a[3]; };
int via_ptr(int (*f)(int, int), int a, int b) { return f(a, b); }
int main() {
  W w{3};
  auto c = make_counter(10);
  int c1 = c(), c2 = c();
  auto gen = [](auto a, auto b) { return a + b; };
  int (*gfp)(int, int) = gen;
  auto outer = [&](int x) { auto inner = [x, &w](int y) { return x * y + w.base; }; return inner(x + 1); };
  Big bg{{1, 2, 3, 4}};
  printf("%d %d %d %d %d %d\n", w.k, w.fp(1), w.run(2), g_init, g_fp(2), c1 * 100 + c2);
  printf("%d %d %d %d\n", via_ptr(gfp, 2, 3), outer(3), big(bg), apply([](int x) { return -x; }, 4));
  return 0;
}
