//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Lambdas: non-capturing (and its conversion to a function pointer), captures
// by value and by reference, mutable, generic, returned by value, stored in a
// class, called through a template, an immediately invoked lambda, nested.
extern "C" int printf(const char *, ...);
template <class F> int apply(F f, int x) { return f(x); }
struct Holder { int k; int run(int x) const { auto l = [this](int y) { return y + k; }; return l(x); } };
auto make_adder(int n) { return [n](int x) { return x + n; }; }
int bad;
int main() {
  int base = 10;
  auto plain = [](int x) { return x * 2; };
  int (*fp)(int) = plain;
  if (fp(4) != 8 || plain(5) != 10) bad |= 1;
  auto byval = [base](int x) { return x + base; };
  base = 100;
  if (byval(1) != 11) bad |= 2;
  auto byref = [&base](int x) { return x + base; };
  base = 200;
  if (byref(1) != 201) bad |= 4;
  int counter = 0;
  auto mut = [counter]() mutable { return ++counter; };
  if (mut() != 1 || mut() != 2 || counter != 0) bad |= 8;
  auto generic = [](auto a, auto b) { return a + b; };
  if (generic(1, 2) != 3 || generic(1.5, 2.5) != 4.0) bad |= 16;
  auto add5 = make_adder(5);
  if (add5(1) != 6 || apply(add5, 2) != 7 || apply(make_adder(8), 2) != 10) bad |= 32;
  Holder h{3};
  if (h.run(4) != 7) bad |= 64;
  int r = [](int x) { return x * x; }(7);
  if (r != 49) bad |= 128;
  auto outer = [](int x) { auto inner = [x](int y) { return x * y; }; return inner(x + 1); };
  if (outer(3) != 12) bad |= 256;
  printf("bad=%d\n", bad);
  return bad != 0;
}
