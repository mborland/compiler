//type:rp
//options:--c++23:--c++23 -O2
//require:BACK_END_TYPE 3
/* cxx23.auto_cast: auto(x) and auto{x}: decay copy as a prvalue. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename A, typename B> struct IsSame { static const bool v = false; };
template <typename A> struct IsSame<A, A> { static const bool v = true; };

static int copies;
struct Obj {
  int v;
  Obj(int x) : v(x) {}
  Obj(const Obj &o) : v(o.v) { ++copies; }
};

static int take(int *p) { return *p; }
static int take_ref(Obj &o) { return o.v; }
static int take_rv(Obj &&o) { return o.v; }
static void bump(Obj &o) { ++o.v; }

static void run()
{
  int a[3] = {4, 5, 6};
  CHECK((IsSame<decltype(auto(a)), int *>::v));
  CHECK(take(auto(a)) == 4);
  const int c = 5;
  CHECK((IsSame<decltype(auto(c)), int>::v));
  CHECK(auto(c) == 5 && auto{c} == 5);
  CHECK((IsSame<decltype(auto{c}), int>::v));
  Obj o(10);
  CHECK(take_rv(auto(o)) == 10 && copies == 1);
  bump(o);
  Obj &r = o;
  CHECK(take_rv(auto{r}) == 11 && copies == 2);
  CHECK(auto(o).v == 11 && o.v == 11);
  const char *s = "text";
  CHECK(auto(s)[1] == 'e');
  const char str[] = "hello";
  CHECK((IsSame<decltype(auto(str)), const char *>::v));
  CHECK(auto(str)[4] == 'o');
  int f = 3;
  CHECK((IsSame<decltype(auto(take)), int (*)(int *)>::v));
  CHECK(auto(f) + auto{f} == 6);
  bump(o);
  CHECK(take_ref(o) == 12);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.auto_cast ok");
  return failures != 0;
}
