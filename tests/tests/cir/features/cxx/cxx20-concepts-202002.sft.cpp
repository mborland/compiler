//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.concepts_202002: concepts, requires clauses and conditionally trivial special member functions (P0848R3). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int copies, dtors;

template <typename T> concept Small = sizeof(T) <= 4;
template <typename T> concept Integral = requires(T t) { t % t; };
template <typename T> concept Addable = requires(T a, T b) { { a + b } -> Small; };

template <typename T> struct Opt {
  T v;
  Opt(T x) : v(x) {}
  Opt(const Opt &) requires Small<T> = default;
  Opt(const Opt &o) requires(!Small<T>) : v(o.v) { ++copies; }
  ~Opt() requires Small<T> = default;
  ~Opt() requires(!Small<T>) { ++dtors; }
};

template <Integral T> T twice(T x) { return x + x; }
template <typename T> requires Addable<T> T add3(T a, T b, T c) { return a + b + c; }
static int sum_all(Integral auto a, Integral auto b) { return (int)a + (int)b; }
template <typename T> struct Is { static constexpr bool value = false; };
template <Integral T> struct Is<T> { static constexpr bool value = true; };

static void run()
{
  CHECK(__is_trivially_copyable(Opt<int>));
  CHECK(!__is_trivially_copyable(Opt<long>));
  CHECK(__has_trivial_destructor(Opt<char>));
  CHECK(!__has_trivial_destructor(Opt<double>));
  {
    Opt<int> a(5);
    Opt<int> b(a);
    CHECK(b.v == 5 && copies == 0);
    Opt<long> c(1L << 40);
    Opt<long> d(c);
    CHECK(d.v == (1L << 40) && copies == 1);
  }
  CHECK(dtors == 2);
  CHECK(twice(21) == 42 && twice('a') == (char)(2 * 'a'));
  CHECK(add3(1, 2, 3) == 6);
  CHECK(sum_all(2, 3L) == 5);
  CHECK(Is<int>::value && !Is<double>::value);
  CHECK(Small<char> && !Small<long>);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.concepts_202002 ok");
  return failures != 0;
}
