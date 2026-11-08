//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.static_assert_no_message: static_assert with no message at namespace, class, block and template scope. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static_assert(sizeof(int) >= 4);
static_assert(true);

namespace N {
static_assert(sizeof(char) == 1);
}

struct S {
  int v[3];
  static_assert(sizeof(int) * 3 <= sizeof(v));
};

template <typename T> struct Check {
  static_assert(sizeof(T) >= 1);
  static_assert(sizeof(T) <= 64);
  T t;
};

template <int N> int fixed()
{
  static_assert(N > 0);
  static_assert(N % 2 == 0);
  return N / 2;
}

static void run()
{
  static_assert(sizeof(long) >= sizeof(int));
  S s = {{1, 2, 3}};
  CHECK(s.v[2] == 3);
  Check<double> c = {2.5};
  CHECK(c.t == 2.5);
  CHECK(fixed<8>() == 4);
  CHECK(fixed<2>() == 1);
  constexpr int k = 5;
  static_assert(k == 5);
  CHECK(k == 5);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.static_assert_no_message ok");
  return failures != 0;
}
