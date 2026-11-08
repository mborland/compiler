//type:rp
//options:--c++11:--c++11 -O2
//require:BACK_END_TYPE 3
/* cxx11.override_final: override and final on virtual functions and classes; both are contextual keywords. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct Base {
  virtual int f() const { return 1; }
  virtual int g() const { return 10; }
  virtual int h() const { return 100; }
  virtual ~Base() {}
};
struct D1 : Base {
  int f() const override { return 2; }
  int g() const override final { return 20; }
};
struct D2 final : D1 {
  int f() const override { return 3; }
  int h() const override { return 300; }
};
struct D3 : Base {
  int f() const final { return 4; }
};

static int call_f(const Base &b) { return b.f(); }

static void run()
{
  D1 d1;
  D2 d2;
  D3 d3;
  Base b;
  const Base *all[] = {&b, &d1, &d2, &d3};
  int f = 0, g = 0, h = 0;
  for (int i = 0; i < 4; ++i) {
    f = f * 10 + all[i]->f();
    g += all[i]->g();
    h += all[i]->h();
  }
  CHECK(f == 1234);
  CHECK(g == 10 + 20 + 20 + 10);
  CHECK(h == 100 + 100 + 300 + 100);
  CHECK(call_f(d2) == 3);
  const D2 &r = d2;
  CHECK(r.f() == 3 && r.D1::f() == 2);
  int final = 3;
  int override = 4;
  CHECK(final + override == 7);
}

int main()
{
  run();
  if (failures == 0) puts("cxx11.override_final ok");
  return failures != 0;
}
