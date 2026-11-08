//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.nodiscard_reason: [[nodiscard("reason")]] on functions and [[nodiscard]] on constructors. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
static int made;

struct Guard {
  int v;
  [[nodiscard("a guard must be named")]] Guard(int x) : v(x) { ++made; }
  Guard(const Guard &o) : v(o.v) { ++made; }
};
struct Plain {
  int v;
  [[nodiscard]] Plain() : v(3) {}
  [[nodiscard]] explicit Plain(int x) : v(x) {}
};

[[nodiscard("check the result")]] static int compute(int x) { return x * 3; }
struct S {
  [[nodiscard("member")]] int get() const { return v; }
  [[nodiscard("static")]] static int make() { return 9; }
  int v;
};
template <typename T> [[nodiscard("tmpl")]] T tw(T x) { return x + x; }

static void run()
{
  Guard g(4);
  CHECK(g.v == 4 && made == 1);
  (void)Guard(5);
  CHECK(made == 2);
  Guard g2 = Guard(6);
  CHECK(g2.v == 6);
  Plain p1;
  Plain p2(8);
  CHECK(p1.v == 3 && p2.v == 8);
  static_cast<void>(Plain(1));
  int a = compute(2);
  CHECK(a == 6);
  (void)compute(1);
  S s = {5};
  CHECK(s.get() == 5 && S::make() == 9);
  (void)s.get();
  CHECK(tw(3) == 6 && tw(1.5) == 3.0);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.nodiscard_reason ok");
  return failures != 0;
}
