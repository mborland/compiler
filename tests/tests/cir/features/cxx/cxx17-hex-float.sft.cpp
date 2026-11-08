//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.hex_float: hexadecimal floating-point literals: values, suffixes, constant expressions. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static volatile double vd;
static volatile float vf;
static volatile long double vl;

static void run()
{
  CHECK(0x1p0 == 1.0 && 0x1p1 == 2.0 && 0x1p-1 == 0.5);
  CHECK(0x1.8p1 == 3.0);
  CHECK(0xA.8p0 == 10.5);
  CHECK(0x.8p1 == 1.0);
  CHECK(0x1.fp3 == 15.5);
  CHECK(0X1P+4 == 16.0);
  CHECK(0x1p-1022 > 0.0);
  CHECK(0x1.fffffffffffffp1023 > 1e308);
  CHECK(0x1.0p-2f == 0.25f && 0x3p0f == 3.0f);
  CHECK(0x1p0L == 1.0L && 0x1.8p-1L == 0.75L);
  constexpr double k = 0x1.4p3;
  static_assert(k == 10.0, "constant");
  vd = 0x1.999999999999ap-4;
  CHECK(vd > 0.0999 && vd < 0.1001);
  vf = 0x1.8p0f;
  CHECK(vf == 1.5f);
  vl = 0x1.8p0L;
  CHECK(vl == 1.5L);
  CHECK(sizeof(0x1p0f) == sizeof(float) && sizeof(0x1p0) == sizeof(double));
  CHECK(sizeof(0x1p0L) == sizeof(long double));
  CHECK(0x1'0p0 == 16.0);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.hex_float ok");
  return failures != 0;
}
