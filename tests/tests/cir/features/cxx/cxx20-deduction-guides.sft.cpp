//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.deduction_guides: class template argument deduction for aggregates and alias templates. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

template <typename T> struct Agg {
  T a;
  T b;
};
template <typename T, typename U> struct Pair {
  T first;
  U second;
};
template <typename T> struct Box {
  T v;
  Box(T x) : v(x) {}
};

template <typename T> using IntFirst = Pair<int, T>;
template <typename T> using BoxAlias = Box<T>;
template <typename T> using Twin = Pair<T, T>;

static void run()
{
  Agg x{1, 2};
  CHECK((IsSame<decltype(x), Agg<int> >::v));
  CHECK(x.a == 1 && x.b == 2);
  Agg y = {1.5, 2.5};
  CHECK((IsSame<decltype(y), Agg<double> >::v));
  CHECK(y.a + y.b == 4.0);
  Pair p{'c', 3L};
  CHECK((IsSame<decltype(p), Pair<char, long> >::v));
  CHECK(p.first == 'c' && p.second == 3L);
  IntFirst f{1, 2.5};
  CHECK((IsSame<decltype(f), Pair<int, double> >::v));
  CHECK(f.first == 1 && f.second == 2.5);
  BoxAlias b(7);
  CHECK((IsSame<decltype(b), Box<int> >::v));
  CHECK(b.v == 7);
  Twin t{4, 5};
  CHECK((IsSame<decltype(t), Pair<int, int> >::v));
  CHECK(t.first + t.second == 9);
  Agg nested{Agg{1, 2}, Agg{3, 4}};
  CHECK((IsSame<decltype(nested), Agg<Agg<int> > >::v));
  CHECK(nested.b.a == 3);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.deduction_guides ok");
  return failures != 0;
}
