//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.lambda_unevaluated: lambdas in unevaluated operands and template arguments; default-constructible and assignable captureless lambdas. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

template <typename T, typename Cmp = decltype([](const T &a, const T &b) { return a < b; })>
struct Sorted {
  T data[8];
  unsigned n;
  Sorted() : n(0) {}
  void insert(T x)
  {
    Cmp less;
    unsigned i = n++;
    while (i > 0 && less(x, data[i - 1])) { data[i] = data[i - 1]; --i; }
    data[i] = x;
  }
};

template <typename F> struct Holder {
  F f;
  int call(int x) { return f(x); }
};

using Inc = decltype([](int x) { return x + 1; });
using Dbl = decltype([](int x) { return x * 2; });

static void run()
{
  Inc inc;
  Inc inc2;
  Dbl dbl;
  CHECK(inc(1) == 2 && dbl(4) == 8);
  CHECK((IsSame<Inc, Inc>::v));
  CHECK((!IsSame<Inc, Dbl>::v));
  inc2 = inc;
  CHECK(inc2(9) == 10);
  Holder<Inc> hi;
  Holder<Dbl> hd;
  CHECK(hi.call(5) == 6 && hd.call(5) == 10);
  Sorted<int> s;
  int in[] = {5, 2, 8, 1, 9, 3};
  for (int v : in) s.insert(v);
  CHECK(s.n == 6);
  CHECK(s.data[0] == 1 && s.data[1] == 2 && s.data[2] == 3 && s.data[5] == 9);
  decltype([] { return 42; }) k;
  CHECK(k() == 42);
  CHECK(sizeof(decltype([](int a) { return a; })) == 1);
  auto fp = +inc;
  CHECK(fp(1) == 2);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.lambda_unevaluated ok");
  return failures != 0;
}
