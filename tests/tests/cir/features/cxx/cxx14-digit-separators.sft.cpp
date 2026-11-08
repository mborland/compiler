//type:rp
//options:--c++14:--c++14 -O2
//require:BACK_END_TYPE 3
/* cxx14.digit_separators: digit separators in integer, floating, hex and binary literals and in the preprocessor. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
#if 1'000 != 1000
#error digit separators in the preprocessor
#endif

static volatile double vd;

static void run()
{
  CHECK(1'000'000 == 1000000);
  CHECK(0xFF'FF == 65535 && 0x1'0000 == 65536);
  CHECK(0'17 == 15);
  CHECK(0b1010'1010 == 170);
  CHECK(1'0.5'0 == 10.5);
  CHECK(1'0e1'0 == 10e10);
  CHECK(123'456'789'012LL == 123456789012LL);
  CHECK(1'0u == 10u && 1'0ull == 10ull);
  CHECK(3.141'592'653 > 3.1415926 && 3.141'592'653 < 3.1415927);
  CHECK('a' == 97 && '\'' == 39);
  const char *s = "1'2";
  CHECK(s[1] == '\'' && s[2] == '2');
  constexpr long big = 9'223'372'036'854'775'807L;
  static_assert(big == 9223372036854775807L, "separators in a constant");
  CHECK(big == 9223372036854775807L);
  vd = 1'0.5'0;
  CHECK(vd == 10.5);
  int arr[1'0];
  CHECK(sizeof arr == 10 * sizeof(int));
}

int main()
{
  run();
  if (failures == 0) puts("cxx14.digit_separators ok");
  return failures != 0;
}
