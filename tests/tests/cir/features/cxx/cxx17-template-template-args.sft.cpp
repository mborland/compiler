//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.template_template_args: template template arguments matching templates with default arguments and packs (P0522R0). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T, typename U = int> struct Two {
  T a;
  U b;
};
template <typename T> struct One { T a; };
template <typename... Ts> struct Many { static const unsigned n = sizeof...(Ts); };

template <template <typename> class TT> struct TakesOne {
  TT<char> value;
  static int size() { return (int)sizeof(TT<char>); }
};
template <template <typename...> class TT> struct TakesPack {
  static unsigned n() { return TT<int, char, long>::n; }
};
template <template <typename, typename> class TT> struct TakesTwo {
  TT<short, long> value;
};
template <template <typename> class TT, typename T> struct Make {
  static TT<T> make(T x) { TT<T> r = {x}; return r; }
};

static void run()
{
  TakesOne<One> a;
  a.value.a = 'q';
  CHECK(a.value.a == 'q' && TakesOne<One>::size() == 1);
  TakesOne<Two> b;
  b.value.a = 'r';
  b.value.b = 5;
  CHECK(b.value.a == 'r' && b.value.b == 5);
  CHECK(TakesOne<Two>::size() == (int)sizeof(Two<char, int>));
  TakesOne<Many> c;
  (void)c;
  CHECK(TakesPack<Many>::n() == 3);
  TakesTwo<Two> t;
  t.value.a = 3;
  t.value.b = 1L << 40;
  CHECK(t.value.a == 3 && t.value.b == (1L << 40));
  CHECK(Make<One, int>::make(9).a == 9);
  CHECK(Make<Two, double>::make(1.5).a == 1.5);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.template_template_args ok");
  return failures != 0;
}
