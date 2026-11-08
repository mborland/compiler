//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.constexpr_202207: constexpr functions that cannot be constant-evaluated, and non-literal parameter and return types (P2448R2). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int counter;
static int bump() { return ++counter; }

constexpr int never_constant() { return bump(); }

struct NonLiteral {
  int v;
  NonLiteral(int x) : v(x) {}
  ~NonLiteral() {}
};
constexpr NonLiteral make(int x) { return NonLiteral(x); }
constexpr int take(NonLiteral n) { return n.v; }
constexpr int maybe(bool b, int x) { return b ? x : bump(); }
static_assert(maybe(true, 3) == 3, "constant path");

static void run()
{
  CHECK(never_constant() == 1);
  CHECK(never_constant() == 2);
  NonLiteral n = make(9);
  CHECK(n.v == 9);
  CHECK(take(make(4)) == 4);
  CHECK(maybe(false, 0) == 3);
  CHECK(maybe(true, 7) == 7);
  CHECK(counter == 3);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.constexpr_202207 ok");
  return failures != 0;
}
