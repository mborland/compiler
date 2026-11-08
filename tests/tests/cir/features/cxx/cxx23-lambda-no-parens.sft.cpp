//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.lambda_no_parens: optional () in lambdas with specifiers, attributes, noexcept and a trailing return type. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static void run()
{
  int x = 1;
  auto m = [x] mutable { return ++x; };
  CHECK(m() == 2 && m() == 3 && x == 1);
  auto ce = [] constexpr { return 5; };
  static_assert(ce() == 5, "constexpr");
  CHECK(ce() == 5);
  auto ne = [] noexcept { return 6; };
  CHECK(noexcept(ne()) && ne() == 6);
  auto both = [y = 10] mutable noexcept { return y += 5; };
  CHECK(both() == 15 && both() == 20);
  auto ret = [] mutable -> long { return 1L << 40; };
  CHECK(ret() == (1L << 40));
  CHECK((IsSame<decltype(ret()), long>::v));
  auto attr = [] [[nodiscard]] { return 7; };
  CHECK(attr() == 7);
  auto cv = [] consteval { return 8; };
  CHECK(cv() == 8);
  auto tpl = []<typename T> mutable { return sizeof(T); };
  CHECK(tpl.operator()<long>() == sizeof(long));
  auto st = [] static { return 9; };
  CHECK(st() == 9);
  int (*fp)() = st;
  CHECK(fp() == 9);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.lambda_no_parens ok");
  return failures != 0;
}
