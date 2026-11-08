//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.generic_lambdas_template: template parameter lists on generic lambdas. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static void run()
{
  auto add = []<typename T>(T a, T b) { return a + b; };
  CHECK(add(1, 2) == 3);
  CHECK(add(1.5, 2.0) == 3.5);
  auto size = []<typename T, unsigned long N>(T (&)[N]) { return N; };
  int a5[5];
  char c3[3];
  CHECK(size(a5) == 5 && size(c3) == 3);
  auto sz = []<typename T>(T) { return sizeof(T); };
  CHECK(sz('x') == 1 && sz(1L) == sizeof(long));
  auto kind = []<typename T>(const T &) { return IsSame<T, int>::v; };
  CHECK(kind(3) && !kind(3.0));
  auto nt = []<int N>() { return N * 2; };
  CHECK(nt.operator()<21>() == 42);
  auto pack = []<typename... Ts>(Ts... xs) { return (0 + ... + (int)xs); };
  CHECK(pack(1, 2, 3) == 6 && pack() == 0);
  auto ret = []<typename T>(T x) -> T { return x * 2; };
  CHECK(ret(4) == 8 && ret(1.25) == 2.5);
  int base = 10;
  auto capt = [base]<typename T>(T x) { return x + base; };
  CHECK(capt(5) == 15 && capt(0.5) == 10.5);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.generic_lambdas_template ok");
  return failures != 0;
}
