//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.nodiscard: [[nodiscard]] on functions, classes and enumerations; results that are used. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
[[nodiscard]] static int compute(int x) { return x * 3; }
struct [[nodiscard]] Status {
  int code;
};
static Status make(int c) { Status s = {c}; return s; }
enum class [[nodiscard]] Err { Ok, Bad };
static Err check(int v) { return v ? Err::Bad : Err::Ok; }
struct Obj {
  [[nodiscard]] int get() const { return v; }
  [[nodiscard]] static Obj mk(int x) { Obj o; o.v = x; return o; }
  int v;
};
template <typename T> [[nodiscard]] T twice(T x) { return x + x; }

static void run()
{
  int a = compute(4);
  CHECK(a == 12);
  CHECK(compute(1) + compute(2) == 9);
  (void)compute(5);
  Status s = make(7);
  CHECK(s.code == 7);
  (void)make(1);
  CHECK(check(0) == Err::Ok && check(3) == Err::Bad);
  (void)check(1);
  Obj o = Obj::mk(8);
  CHECK(o.get() == 8);
  static_cast<void>(o.get());
  CHECK(twice(4) == 8 && twice(1.5) == 3.0);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.nodiscard ok");
  return failures != 0;
}
