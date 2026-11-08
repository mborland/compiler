//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.conditional_explicit: explicit(bool): conversions allowed or forbidden by the condition. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> T &&dv();

template <typename From, typename To> auto conv_test(int) -> decltype(static_cast<void (*)(To)>(0)(dv<From>()), true) { return true; }
template <typename From, typename To> bool conv_test(...) { return false; }
template <typename From, typename To> constexpr bool implicitly_convertible = false;
template <typename From, typename To> bool can_convert() { return conv_test<From, To>(0); }

template <typename T> struct W {
  T v;
  template <typename U> explicit(sizeof(U) > 4) W(U u) : v((T)u) {}
};
struct E1 {
  explicit(true) E1(int) {}
};
struct E2 {
  explicit(false) E2(int) {}
};
template <bool B> struct Cond {
  explicit(B) Cond(int x) : v(x) {}
  int v;
};
template <bool B> struct ConvOp {
  explicit(B) operator int() const { return 5; }
};

static int takes_w(W<long> w) { return (int)w.v; }

static void run()
{
  CHECK((can_convert<int, W<long> >()));
  CHECK((!can_convert<long, W<long> >()));
  CHECK((!can_convert<int, E1>()));
  CHECK((can_convert<int, E2>()));
  CHECK((can_convert<int, Cond<false> >()));
  CHECK((!can_convert<int, Cond<true> >()));
  CHECK(takes_w(3) == 3);
  W<long> w2(1L << 40);
  CHECK(w2.v == (1L << 40));
  E1 e1(1);
  E2 e2 = 2;
  (void)e1;
  (void)e2;
  Cond<true> c1(8);
  Cond<false> c2 = 9;
  CHECK(c1.v == 8 && c2.v == 9);
  ConvOp<false> o1;
  int i1 = o1;
  CHECK(i1 == 5);
  ConvOp<true> o2;
  CHECK(static_cast<int>(o2) == 5);
  CHECK((can_convert<ConvOp<false>, int>()));
  CHECK((!can_convert<ConvOp<true>, int>()));
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.conditional_explicit ok");
  return failures != 0;
}
