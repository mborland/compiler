//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.size_t_suffix: the uz and z literal suffixes: size_t and its signed counterpart. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static volatile unsigned long vu;

static void run()
{
  CHECK((IsSame<decltype(0uz), unsigned long>::v));
  CHECK((IsSame<decltype(0zu), unsigned long>::v));
  CHECK((IsSame<decltype(0z), long>::v));
  CHECK((IsSame<decltype(0UZ), unsigned long>::v));
  CHECK((IsSame<decltype(0Zu), unsigned long>::v));
  CHECK((IsSame<decltype(0uz), decltype(sizeof 0)>::v));
  CHECK(1uz == 1 && 42z == 42 && 0x10uz == 16 && 010z == 8 && 0b101uz == 5);
  CHECK(-1z == -1L);
  CHECK(1'000'000uz == 1000000ul);
  CHECK(4294967296uz == 4294967296ul);
  CHECK(9223372036854775807z == 9223372036854775807L);
  vu = 18446744073709551615uz;
  CHECK(vu == ~0ul);
  auto n = 5uz;
  auto m = 5z;
  CHECK(sizeof n == sizeof(unsigned long) && sizeof m == sizeof(long));
  CHECK((m - 6z) < 0 && (n - 6uz) > 0);
  int arr[3uz];
  CHECK(sizeof arr / sizeof arr[0] == 3uz);
  long sum = 0;
  for (auto i = 0z; i < 5z; ++i) sum += i;
  CHECK(sum == 10);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.size_t_suffix ok");
  return failures != 0;
}
