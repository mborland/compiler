//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.deduction_guides: class template argument deduction: implicit guides, user guides, copy deduction, aggregates not included. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> struct Box {
  T v;
  Box(T x) : v(x) {}
};

template <typename T, typename U> struct Pair {
  T first;
  U second;
  Pair(T a, U b) : first(a), second(b) {}
};

template <typename T> struct Vec {
  T data[8];
  unsigned n;
  template <typename It> Vec(It b, It e) : n(0)
  {
    for (; b != e; ++b) data[n++] = *b;
  }
  Vec(unsigned count, T fill) : n(count)
  {
    for (unsigned i = 0; i < count; ++i) data[i] = fill;
  }
};
template <typename It> Vec(It, It) -> Vec<typename It::value_type>;

struct IntIt {
  typedef int value_type;
  int cur;
  int operator*() const { return cur; }
  IntIt &operator++() { ++cur; return *this; }
  bool operator!=(const IntIt &o) const { return cur != o.cur; }
};

template <typename T> struct Wrap {
  T t;
  explicit Wrap(const T &x) : t(x) {}
};
Wrap(const char *) -> Wrap<const char *>;

template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static void run()
{
  Box b(3);
  CHECK((IsSame<decltype(b), Box<int> >::v));
  CHECK(b.v == 3);
  Box c = b;
  CHECK((IsSame<decltype(c), Box<int> >::v));
  Box d(2.5);
  CHECK((IsSame<decltype(d), Box<double> >::v));
  Pair p(1, 'x');
  CHECK((IsSame<decltype(p), Pair<int, char> >::v));
  CHECK(p.first == 1 && p.second == 'x');
  IntIt i0 = {3}, i1 = {7};
  Vec v(i0, i1);
  CHECK((IsSame<decltype(v), Vec<int> >::v));
  CHECK(v.n == 4 && v.data[0] == 3 && v.data[3] == 6);
  Vec w(3u, 1.5);
  CHECK((IsSame<decltype(w), Vec<double> >::v));
  CHECK(w.n == 3 && w.data[2] == 1.5);
  Wrap wr("abc");
  CHECK((IsSame<decltype(wr), Wrap<const char *> >::v));
  CHECK(wr.t[1] == 'b');
  Box<Box<int> > nested(Box<int>(8));
  Box dn(nested);
  CHECK((IsSame<decltype(dn), Box<Box<int> > >::v));
  CHECK(dn.v.v == 8);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.deduction_guides ok");
  return failures != 0;
}
