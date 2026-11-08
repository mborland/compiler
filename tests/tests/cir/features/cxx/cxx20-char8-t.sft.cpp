//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.char8_t: char8_t: UTF-8 literals have type char8_t, unsigned, one byte. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static unsigned len8(const char8_t *s)
{
  unsigned n = 0;
  while (s[n]) ++n;
  return n;
}

static void run()
{
  CHECK(sizeof(char8_t) == 1);
  CHECK((IsSame<decltype(u8'a'), char8_t>::v));
  CHECK((IsSame<decltype(u8"abc"[0]), const char8_t &>::v));
  CHECK((IsSame<decltype(u8"abc"), const char8_t(&)[4]>::v));
  CHECK(u8'a' == 97 && u8'Z' == 90);
  char8_t hi = u8'\x7f';
  CHECK(hi == 127);
  char8_t big = (char8_t)0xE9;
  CHECK(big == 0xE9 && big > 0);
  CHECK(static_cast<char8_t>(-1) == 255);
  const char8_t *s = u8"hello";
  CHECK(len8(s) == 5 && s[0] == u8'h' && s[4] == u8'o');
  CHECK(len8(u8"") == 0);
  char8_t arr[] = u8"ab";
  CHECK(sizeof arr == 3);
  char8_t seq[] = {u8'x', u8'y', 0};
  CHECK(len8(seq) == 2);
  CHECK(!(IsSame<char8_t, char>::v) && !(IsSame<char8_t, unsigned char>::v));
  char8_t acc = 250;
  acc += 10;
  CHECK(acc == 4);
  switch (s[1]) {
  case u8'e': break;
  default: CHECK(false);
  }
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.char8_t ok");
  return failures != 0;
}
