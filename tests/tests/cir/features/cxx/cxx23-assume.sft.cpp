//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.assume: [[assume(expr)]]: the program is unchanged when the assumption holds. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int idx(int i, const int *tab)
{
  [[assume(i >= 0 && i < 4)]];
  return tab[i];
}

static int div16(int x)
{
  [[assume(x % 16 == 0)]];
  return x / 16;
}

static int loop(int n)
{
  [[assume(n > 0)]];
  int s = 0;
  for (int i = 0; i < n; ++i) s += i;
  return s;
}

static int sw(int x)
{
  switch (x) {
  case 0: return 10;
  case 1: return 11;
  case 2: return 12;
  default:
    [[assume(false)]];
  }
  return -1;
}

static void run()
{
  const int tab[4] = {5, 6, 7, 8};
  CHECK(idx(0, tab) == 5 && idx(3, tab) == 8);
  CHECK(div16(64) == 4 && div16(0) == 0);
  CHECK(loop(5) == 10 && loop(1) == 0);
  CHECK(sw(0) == 10 && sw(2) == 12);
  int v = 9;
  [[assume(v == 9)]];
  CHECK(v == 9);
  [[assume(sizeof(int) >= 4)]];
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.assume ok");
  return failures != 0;
}
