//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.consteval_202211: consteval propagates up: immediate-escalating function templates and lambdas, and nested immediate functions. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
consteval int sq(int x) { return x * x; }

template <typename T> constexpr T tsq(T x) { return sq(x) + 1; }
static_assert(tsq(5) == 26, "template");

template <typename T> constexpr int pick(bool b, T x)
{
  if (b) return 0;
  return sq(x);
}
static_assert(pick(true, 4) == 0 && pick(false, 3) == 9, "template with a branch");

constexpr auto lam = [](int x) { return sq(x) * 2; };
static_assert(lam(3) == 18, "lambda");

template <typename T> struct S {
  int v;
  constexpr S(T x) : v(sq(x)) {}
};
static_assert(S<int>(4).v == 16, "constructor of a class template");

consteval int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
template <typename T> constexpr int via(T n) { return fact(n); }

static void run()
{
  constexpr int a = tsq(4);
  CHECK(a == 17);
  constexpr int b = pick(false, 5);
  CHECK(b == 25);
  constexpr int d = lam(4);
  CHECK(d == 32);
  constexpr S<int> s(5);
  CHECK(s.v == 25);
  constexpr int e = via(5);
  CHECK(e == 120);
  CHECK(sq(7) == 49 && fact(4) == 24);
  int arr[sq(3)];
  CHECK(sizeof arr == 9 * sizeof(int));
  int rt = 6;
  auto ok = [&](int k) { return k + rt; };
  CHECK(ok(sq(2)) == 10);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.consteval_202211 ok");
  return failures != 0;
}
