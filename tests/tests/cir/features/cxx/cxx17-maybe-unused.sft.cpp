//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.maybe_unused: [[maybe_unused]] on variables, parameters, functions, types, members and enumerators. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
[[maybe_unused]] static int unused_global = 5;
[[maybe_unused]] static int unused_fn(int x) { return x; }
struct [[maybe_unused]] UnusedClass { int v; };
typedef int UnusedInt [[maybe_unused]];
enum [[maybe_unused]] UnusedEnum { UE_A [[maybe_unused]] = 1, UE_B = 2 };
struct Mem {
  [[maybe_unused]] int a;
  int b;
};

static int takes([[maybe_unused]] int a, int b, [[maybe_unused]] int c) { return b; }
template <typename T> static int tmpl([[maybe_unused]] T t) { return 3; }

static void run()
{
  [[maybe_unused]] int local = 1;
  [[maybe_unused]] static int sl = 2;
  int used = 4;
  CHECK(takes(1, 2, 3) == 2);
  CHECK(tmpl(5.5) == 3 && tmpl('c') == 3);
  Mem m = {1, 6};
  CHECK(m.b == 6);
  CHECK(used == 4);
  [[maybe_unused]] auto lam = [](int x) { return x + 1; };
  UnusedClass uc = {9};
  CHECK(uc.v == 9);
  CHECK(unused_global == 5 && unused_fn(3) == 3);
  UnusedInt ui = 7;
  CHECK(ui == 7 && UE_B == 2);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.maybe_unused ok");
  return failures != 0;
}
