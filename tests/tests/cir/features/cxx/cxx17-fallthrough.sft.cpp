//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.fallthrough: [[fallthrough]] between switch labels. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int classify(int x)
{
  int r = 0;
  switch (x) {
  case 0:
    r += 1;
    [[fallthrough]];
  case 1:
    r += 10;
    [[fallthrough]];
  case 2:
    r += 100;
    break;
  case 3:
    r += 1000;
    [[fallthrough]];
  default:
    r += 5;
  }
  return r;
}

static int walk(int n)
{
  int steps = 0;
  for (int i = 0; i < n; ++i) {
    switch (i % 3) {
    case 0:
      ++steps;
      [[fallthrough]];
    case 1:
      steps += 2;
      break;
    default:
      steps += 4;
    }
  }
  return steps;
}

static void run()
{
  CHECK(classify(0) == 111);
  CHECK(classify(1) == 110);
  CHECK(classify(2) == 100);
  CHECK(classify(3) == 1005);
  CHECK(classify(9) == 5);
  CHECK(walk(0) == 0);
  CHECK(walk(3) == 3 + 2 + 4);
  CHECK(walk(7) == 3 + 2 + 4 + 3 + 2 + 4 + 3);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.fallthrough ok");
  return failures != 0;
}
