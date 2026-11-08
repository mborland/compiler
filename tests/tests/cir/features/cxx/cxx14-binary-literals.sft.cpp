//type:rp
//options:--c++14:--c++14 -O2
//require:BACK_END_TYPE 3
/* cxx14.binary_literals: binary literals: values, suffixes, types, constant expressions. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> struct IsLong { enum { v = 0 }; };
template <> struct IsLong<long> { enum { v = 1 }; };
template <typename T> struct IsUInt { enum { v = 0 }; };
template <> struct IsUInt<unsigned> { enum { v = 1 }; };

static unsigned volatile vsink;

static void run()
{
  CHECK(0b0 == 0 && 0b1 == 1 && 0B101 == 5);
  CHECK(0b11111111 == 255);
  CHECK(0b1010'1010 == 170);
  CHECK(0b1u == 1u && 0b11UL == 3UL && 0b111ULL == 7ULL);
  CHECK(0b1111111111111111111111111111111 == 2147483647);
  CHECK(IsLong<decltype(0b100000000000000000000000000000000)>::v == 1);
  CHECK(IsUInt<decltype(0b10000000000000000000000000000000u)>::v == 1);
  CHECK(0b11111111111111111111111111111111u == 4294967295u);
  CHECK(-0b101 == -5);
  CHECK((0b1100 & 0b1010) == 0b1000 && (0b1100 | 0b0011) == 0b1111);
  constexpr int k = 0b10000;
  static_assert(k == 16, "binary literal in a constant expression");
  char buf[0b1000];
  CHECK(sizeof buf == 8);
  enum { E = 0b110 };
  CHECK(E == 6);
  vsink = 0b101;
  CHECK((vsink << 0b10) == 20);
  unsigned u = 0b1000000000000000000000000000000000000000000000000000000000000000ull >> 0b111110;
  CHECK(u == 2);
}

int main()
{
  run();
  if (failures == 0) puts("cxx14.binary_literals ok");
  return failures != 0;
}
