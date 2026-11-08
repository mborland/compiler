//type:rp
//options:--c++11:--c++11 -O2
//require:BACK_END_TYPE 3
/* cxx11.trailing_return: trailing return types on functions, members, lambdas, function-pointer returns. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T, typename U> auto add(T a, U b) -> decltype(a + b) { return a + b; }
auto plus1(int x) -> int { return x + 1; }
static auto fp(int) -> int (*)(int) { return plus1; }
static auto arr() -> int (&)[3] { static int a[3] = {4, 5, 6}; return a; }

struct S {
  int v;
  auto get() const -> int;
  static auto make(int x) -> S { S s = {x}; return s; }
  auto self() -> S & { return *this; }
  template <typename T> auto scaled(T k) const -> decltype(v * k) { return v * k; }
};
auto S::get() const -> int { return v; }

static void run()
{
  CHECK(add(1, 2.5) == 3.5);
  CHECK(add(1, 2) == 3);
  CHECK(fp(0)(41) == 42);
  CHECK(arr()[2] == 6);
  S s = S::make(7);
  CHECK(s.get() == 7);
  s.self().v = 9;
  CHECK(s.get() == 9);
  CHECK(s.scaled(2.5) == 22.5);
  auto lam = [](int x) -> long { return (long)x * 1000000000L; };
  CHECK(lam(5) == 5000000000L);
  auto lam2 = [](double x) -> double { return x * 2; };
  CHECK(lam2(1.25) == 2.5);
  auto nested = [](int x) -> int (*)(int) { return plus1; };
  CHECK(nested(0)(1) == 2);
}

int main()
{
  run();
  if (failures == 0) puts("cxx11.trailing_return ok");
  return failures != 0;
}
