//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.char8_t: char8_t compatibility: UTF-8 string literals initialize arrays of char and unsigned char. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static void run()
{
  const char a[] = u8"abc";
  CHECK(sizeof a == 4 && a[0] == 'a' && a[2] == 'c' && a[3] == 0);
  const unsigned char b[] = u8"xyz";
  CHECK(sizeof b == 4 && b[1] == 'y' && b[3] == 0);
  char c[8] = u8"hi";
  CHECK(c[0] == 'h' && c[1] == 'i' && c[2] == 0 && c[7] == 0);
  unsigned char d[] = u8"\xC3\xA9";
  CHECK(sizeof d == 3 && d[0] == 0xC3 && d[1] == 0xA9);
  const char8_t e[] = u8"q";
  CHECK(sizeof e == 2 && e[0] == u8'q');
  static const unsigned char tbl[] = u8"0123456789";
  int sum = 0;
  for (unsigned i = 0; i < 10; ++i) sum += tbl[i] - '0';
  CHECK(sum == 45);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.char8_t ok");
  return failures != 0;
}
