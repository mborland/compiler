//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
template <class F> int apply(F f, int x) { return f(x); }
struct Holder { int k; int run(int x) const { auto l = [this](int y) { return y + k; }; return l(x); } };
auto make_adder(int n) { return [n](int x) { return x + n; }; }
int f(int base) {
  auto byval = [base](int x) { return x + base; };
  int r = byval(1);
  auto plain = [](int x) { return x * 2; };
  int (*fp)(int) = plain;
  auto byref = [&base](int x) { return x + base; };
  int counter = 0;
  auto mut = [counter]() mutable { return ++counter; };
  auto generic = [](auto a, auto b) { return a + b; };
  r += generic(1, 2) + (int)generic(1.5, 2.5);
  r += apply(make_adder(8), 2) + [](int x) { return x * x; }(7);
  return r + fp(2) + byref(1) + mut();
}
struct W { int base; int k = [] { return 7; }(); int (*fp)(int) = [](int x) { return x + 1; }; };
int g_init = [] { return 5; }();
int (*g_fp)(int) = [](int x) { return x * 3; };
int use_w(W *w) { return w->k + w->fp(1); }
static int local1() { return apply([](int a) { return a + 1; }, 10); }
namespace { auto ns_lambda = [](int a) { return a * 5; }; }
int external_user() { return local1() + apply(ns_lambda, 2); }
