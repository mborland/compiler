//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.constexpr_202110: constexpr functions with non-literal variables and gotos that are not evaluated (P2242R3). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int made, killed;

struct NonLiteral {
  NonLiteral() { ++made; }
  ~NonLiteral() { ++killed; }
};

constexpr int guarded(int x)
{
  if (x < 0) {
    NonLiteral nl;
    return -1;
  }
  return x * 2;
}
static_assert(guarded(4) == 8, "constant path");

constexpr int jumpy(int n)
{
  int i = 0;
  if (n < 0) {
  again:
    ++i;
    if (i < 3) goto again;
    return i * 100;
  }
  return n;
}
static_assert(jumpy(5) == 5, "constant path with a label");

constexpr int labels(int n)
{
  if (n == 1) goto done;
  n += 10;
done:
  return n;
}

static void run()
{
  CHECK(guarded(3) == 6);
  CHECK(made == 0);
  CHECK(guarded(-2) == -1);
  CHECK(made == 1 && killed == 1);
  CHECK(jumpy(-1) == 300);
  CHECK(jumpy(9) == 9);
  CHECK(labels(1) == 1 && labels(2) == 12);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.constexpr_202110 ok");
  return failures != 0;
}
