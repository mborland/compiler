//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.utf8_char_literals: u8 character literals have type char in C++17 and the encoded value. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static volatile char vc;

static void run()
{
  CHECK(u8'a' == 'a' && u8'a' == 97);
  CHECK(u8'0' == 48 && u8'Z' == 90);
  CHECK(sizeof(u8'x') == 1);
  CHECK((IsSame<decltype(u8'a'), char>::v));
  CHECK(u8'\x7f' == 127);
  CHECK(u8'\n' == 10 && u8'\\' == 92 && u8'\'' == 39);
  constexpr char c = u8'q';
  static_assert(c == 'q', "constant");
  char buf[u8'\x03'];
  CHECK(sizeof buf == 3);
  vc = u8'z';
  CHECK(vc == 'z');
  const char tab[] = {u8'a', u8'b', u8'c', 0};
  CHECK(tab[0] == 'a' && tab[2] == 'c' && tab[3] == 0);
  switch (vc) {
  case u8'z': break;
  default: CHECK(false);
  }
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.utf8_char_literals ok");
  return failures != 0;
}
