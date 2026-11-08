//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.range_based_for: range-based for: temporaries in the range initializer live for the whole loop (P2718R0). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int live;
static int dtors;

struct Temp {
  int a[3];
  Temp() { ++live; a[0] = 1; a[1] = 2; a[2] = 3; }
  Temp(const Temp &) { ++live; }
  ~Temp() { --live; ++dtors; }
  const int *begin() const { return a; }
  const int *end() const { return a + 3; }
  const Temp &self() const { return *this; }
};
static Temp make() { return Temp(); }

struct Wrapper {
  Temp t;
  const Temp &get() const { return t; }
};
static Wrapper wrap() { return Wrapper(); }

static void run()
{
  int sum = 0;
  int live_in_loop = -1;
  for (int v : make().self()) {
    sum += v;
    live_in_loop = live;
  }
  CHECK(sum == 6);
  CHECK(live_in_loop == 1);
  CHECK(live == 0);
  sum = 0;
  live_in_loop = -1;
  for (int v : wrap().get()) {
    sum += v;
    live_in_loop = live;
  }
  CHECK(sum == 6 && live_in_loop == 1 && live == 0);
  int count = 0;
  for (int v : make().self()) {
    if (live != 1) ++count;
    (void)v;
  }
  CHECK(count == 0);
  for (int v : make()) { if (live != 1) ++count; (void)v; }
  CHECK(count == 0);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.range_based_for ok");
  return failures != 0;
}
