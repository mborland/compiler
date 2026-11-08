//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.variadic_using: pack expansions in using-declarations: overload sets from several bases. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct A { int f(int x) { return x + 1; } };
struct B { int f(char) { return 100; } int g() { return 7; } };
struct C { long f(long x) { return x * 3; } };

template <typename... Bs> struct D : Bs... {
  using Bs::f...;
};
template <typename... Bs> struct Overloaded : Bs... {
  using Bs::operator()...;
};
template <typename... Bs> Overloaded(Bs...) -> Overloaded<Bs...>;
template <typename... Bs> struct E : Bs... {
  using Bs::Bs...;
  E() {}
};
struct S1 { int v; S1(int x) : v(x) {} S1() : v(0) {} };
struct S2 { int w; S2(long x, int y) : w((int)x + y) {} S2() : w(0) {} };

static void run()
{
  D<A, B, C> d;
  CHECK(d.f(1) == 2);
  CHECK(d.f('x') == 100);
  CHECK(d.f(5L) == 15);
  D<A> d1;
  CHECK(d1.f(4) == 5);
  Overloaded o{[](int x) { return x + 1; }, [](const char *s) { return (int)s[0]; }};
  CHECK(o(1) == 2);
  CHECK(o("z") == 'z');
  E<S1, S2> e(5);
  CHECK(e.S1::v == 5);
  E<S1, S2> e2(1L, 2);
  CHECK(e2.S2::w == 3);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.variadic_using ok");
  return failures != 0;
}
