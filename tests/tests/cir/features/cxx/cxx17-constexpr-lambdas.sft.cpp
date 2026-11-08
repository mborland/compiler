//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.constexpr_lambdas: constexpr lambdas: compile-time calls, captures, in static_assert, also called at run time. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
constexpr auto sq = [](int x) { return x * x; };
constexpr auto add = [](int a, int b) constexpr { return a + b; };
static_assert(sq(5) == 25, "sq");
static_assert(add(2, 3) == 5, "add");

constexpr int nested = [](int a) { return [a](int b) { return a * 10 + b; }(3); }(4);
static_assert(nested == 43, "nested");

constexpr int sum_to(int n)
{
  auto step = [](int acc, int i) { return acc + i; };
  int s = 0;
  for (int i = 1; i <= n; ++i) s = step(s, i);
  return s;
}
static_assert(sum_to(10) == 55, "sum_to");

struct Tab {
  int v[4];
  constexpr Tab() : v()
  {
    auto init = [](int i) { return i * i + 1; };
    for (int i = 0; i < 4; ++i) v[i] = init(i);
  }
};
constexpr Tab tab;

template <typename F> constexpr int twice(F f, int x) { return f(f(x)); }

static void run()
{
  constexpr int c1 = sq(7);
  CHECK(c1 == 49);
  int n = 6;
  CHECK(sq(n) == 36);
  CHECK(add(n, 4) == 10);
  CHECK(nested == 43);
  CHECK(sum_to(n) == 21);
  CHECK(tab.v[0] == 1 && tab.v[1] == 2 && tab.v[2] == 5 && tab.v[3] == 10);
  constexpr int t = twice([](int x) { return x + 3; }, 1);
  static_assert(t == 7, "twice");
  CHECK(t == 7);
  CHECK(twice(sq, n) == 1296);
  int k = 100;
  auto cap = [k](int x) constexpr { return x + k; };
  CHECK(cap(5) == 105);
  constexpr auto fp = +[](int x) { return x - 1; };
  CHECK(fp(10) == 9);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.constexpr_lambdas ok");
  return failures != 0;
}
