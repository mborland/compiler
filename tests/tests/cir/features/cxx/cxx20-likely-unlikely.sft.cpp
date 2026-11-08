//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.likely_unlikely: [[likely]] and [[unlikely]] on statements and labels; semantics are unchanged. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int branchy(int x)
{
  int r = 0;
  if (x > 10) [[likely]] {
    r = 1;
  } else [[unlikely]] {
    r = 2;
  }
  if (x == 0) [[unlikely]] r += 100;
  switch (x) {
  [[likely]] case 1: r += 10; break;
  [[unlikely]] case 2: r += 20; break;
  case 3: [[fallthrough]];
  [[likely]] default: r += 5;
  }
  return r;
}

static int loops(int n)
{
  int s = 0;
  for (int i = 0; i < n; ++i) [[likely]] {
    if (i % 5 == 0) [[unlikely]] continue;
    s += i;
  }
  while (n > 0) [[likely]] { --n; ++s; }
  return s;
}

static void run()
{
  CHECK(branchy(11) == 1 + 5);
  CHECK(branchy(1) == 2 + 10);
  CHECK(branchy(2) == 2 + 20);
  CHECK(branchy(3) == 2 + 5);
  CHECK(branchy(0) == 2 + 100 + 5);
  CHECK(loops(10) == (1 + 2 + 3 + 4 + 6 + 7 + 8 + 9) + 10);
  CHECK(loops(0) == 0);
  int v = 7;
  if (v > 3) [[likely]] v = 1; else v = 2;
  CHECK(v == 1);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.likely_unlikely ok");
  return failures != 0;
}
