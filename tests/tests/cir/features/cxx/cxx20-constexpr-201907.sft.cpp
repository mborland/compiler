//type:rp
//options:--c++20:--c++20 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* cxx20.constexpr_201907: constexpr virtual functions, try blocks, changing the active union member, dynamic_cast and typeid. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
#include <typeinfo>

struct Shape {
  virtual constexpr int area() const { return 0; }
  virtual constexpr int sides() const { return 0; }
  constexpr virtual ~Shape() {}
};
struct Sq : Shape {
  int s;
  constexpr Sq(int x) : s(x) {}
  constexpr int area() const override { return s * s; }
  constexpr int sides() const override { return 4; }
};
struct Tri : Shape {
  int b, h;
  constexpr Tri(int x, int y) : b(x), h(y) {}
  constexpr int area() const override { return b * h / 2; }
};

constexpr int total()
{
  Sq a(3);
  Tri t(4, 5);
  const Shape *all[] = {&a, &t};
  int sum = 0;
  for (const Shape *p : all) sum += p->area() * 10 + p->sides();
  return sum;
}
static_assert(total() == 9 * 10 + 4 + 10 * 10 + 0, "virtual");

constexpr int with_try(int x)
{
  try {
    if (x < 0) return -1;
    return x * 2;
  } catch (...) {
    return 0;
  }
}
static_assert(with_try(4) == 8 && with_try(-3) == -1, "try");

union U {
  int i;
  float f;
};
constexpr int active()
{
  U u{};
  u.i = 7;
  int a = u.i;
  u.f = 2.0f;
  return a + (int)u.f;
}
static_assert(active() == 9, "union");

constexpr int dyn(const Shape &s)
{
  const Sq *q = dynamic_cast<const Sq *>(&s);
  return q ? q->s : -1;
}
constexpr int dyn_test()
{
  Sq s(6);
  Tri t(1, 2);
  return dyn(s) * 100 + dyn(t);
}
static_assert(dyn_test() == 600 - 1, "dynamic_cast");

struct Base { constexpr virtual ~Base() {} };
struct Derived : Base {};
static bool same_type(const Base &a, const Base &b) { return typeid(a) == typeid(b); }

static void run()
{
  constexpr int t = total();
  CHECK(t == 194);
  CHECK(total() == 194);
  int x = -5, y = 6;
  CHECK(with_try(x) == -1 && with_try(y) == 12);
  CHECK(active() == 9);
  Sq sq(5);
  Tri tr(2, 2);
  CHECK(dyn(sq) == 5 && dyn(tr) == -1);
  Derived d1, d2;
  Base b;
  CHECK(same_type(d1, d2) && !same_type(d1, b));
  const Shape &r = sq;
  CHECK(r.area() == 25 && r.sides() == 4);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.constexpr_201907 ok");
  return failures != 0;
}
