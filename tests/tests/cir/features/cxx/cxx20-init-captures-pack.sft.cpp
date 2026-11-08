//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.init_captures_pack: pack expansions in lambda init-captures. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename... Ts> int sum_captured(Ts... xs)
{
  auto f = [...vals = xs] { return (0 + ... + (int)vals); };
  return f();
}

template <typename... Ts> int count_after(Ts... xs)
{
  auto f = [...ys = xs]() mutable { return sizeof...(ys) + (0 + ... + (int)(ys += 1)); };
  return (int)f();
}

template <typename F, typename... Args> auto bind_all(F f, Args... args)
{
  return [f, ...a = args](int extra) { return f(a..., extra); };
}

static int add3(int a, int b, int c) { return a + b + c; }
static int add2(int a, int b) { return a + b; }

static void run()
{
  CHECK(sum_captured(1, 2, 3) == 6);
  CHECK(sum_captured() == 0);
  CHECK(count_after(1, 2, 3) == 3 + 2 + 3 + 4);
  auto b = bind_all(add3, 10, 20);
  CHECK(b(5) == 35);
  auto b2 = bind_all(add2, 7);
  CHECK(b2(1) == 8);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.init_captures_pack ok");
  return failures != 0;
}
