//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.nontype_template_parameter_auto: template<auto>: values of several types, pointers, packs, decltype of the parameter. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <auto V> struct Const {
  static constexpr auto value = V;
  typedef decltype(V) type;
};
template <auto... Vs> struct List {
  static constexpr unsigned n = sizeof...(Vs);
};
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static int sq(int x) { return x * x; }
template <auto F> int apply(int x) { return F(x); }
template <auto P> int deref() { return *P; }
static int gv = 77;

template <auto... Vs> constexpr long sum() { return (0 + ... + (long)Vs); }
template <auto First, decltype(First)... Rest> constexpr int count() { return 1 + sizeof...(Rest); }

static void run()
{
  CHECK(Const<5>::value == 5);
  CHECK((IsSame<Const<5>::type, int>::v));
  CHECK((IsSame<Const<'a'>::type, char>::v));
  CHECK((IsSame<Const<5u>::type, unsigned>::v));
  CHECK((IsSame<Const<true>::type, bool>::v));
  CHECK((IsSame<Const<5L>::type, long>::v));
  CHECK(Const<'a'>::value == 'a');
  CHECK(List<1, 'c', true>::n == 3);
  CHECK(List<>::n == 0);
  CHECK(apply<sq>(7) == 49);
  CHECK(apply<&sq>(8) == 64);
  CHECK(deref<&gv>() == 77);
  CHECK(sum<1, 2, 3>() == 6);
  CHECK(sum<1, 'a', 2L>() == 100);
  CHECK((count<1, 2, 3>() == 3));
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.nontype_template_parameter_auto ok");
  return failures != 0;
}
