//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.constexpr_202211: static constexpr variables in constexpr functions (P2647R1). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
constexpr int lookup(int i)
{
  static constexpr int tab[] = {10, 20, 30, 40};
  return tab[i];
}
static_assert(lookup(2) == 30, "table");

constexpr const char *name(int i)
{
  static constexpr const char *names[] = {"zero", "one", "two"};
  return names[i];
}

constexpr int sum_static()
{
  static constexpr int a = 5;
  static constexpr int b = a * 2;
  return a + b;
}
static_assert(sum_static() == 15, "dependent statics");

template <typename T> constexpr T scale(T x)
{
  static constexpr int factor = 3;
  return x * factor;
}
static_assert(scale(4) == 12, "template");

static void run()
{
  int i = 3;
  CHECK(lookup(i) == 40 && lookup(0) == 10);
  CHECK(name(1)[0] == 'o' && name(2)[1] == 'w');
  CHECK(sum_static() == 15);
  CHECK(scale(1.5) == 4.5);
  constexpr int c = lookup(1);
  CHECK(c == 20);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.constexpr_202211 ok");
  return failures != 0;
}
