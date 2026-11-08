//type:rp
//options:--c++11:--c++11 -O2
//require:BACK_END_TYPE 3
/* cxx11.alias_templates: alias templates, including arrays, function pointers, dependent and nested aliases. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> struct Box { T v; };
template <typename T> using BoxPtr = Box<T> *;
template <typename T, int N> using Arr = T[N];
template <typename T> using Fn = T (*)(T);
template <typename T> using Same = T;
template <typename T> using BoxOf = Box<Same<T> >;
template <typename T> using BoxBoxPtr = BoxPtr<BoxOf<T> >;  /* Box<Box<T> > * */
template <typename T, typename U> struct Pair { T a; U b; };
template <typename U> using IntPair = Pair<int, U>;

static int twice(int x) { return 2 * x; }
static double half(double x) { return x / 2; }

template <typename T> T call(Fn<T> f, T x) { return f(x); }

template <typename T> struct Holder {
  using Ptr = BoxPtr<T>;
  Ptr p;
  T get() const { return p->v; }
};

static void run()
{
  Box<int> b = {3};
  BoxPtr<int> p = &b;
  p->v += 4;
  CHECK(b.v == 7);
  Arr<int, 3> a = {1, 2, 3};
  CHECK(a[0] + a[1] + a[2] == 6);
  CHECK(sizeof(Arr<char, 7>) == 7);
  CHECK(sizeof(Arr<Arr<short, 3>, 4>) == 24);
  CHECK(call<int>(twice, 5) == 10);
  CHECK(call<double>(half, 5.0) == 2.5);
  BoxOf<double> bd = {1.5};
  CHECK(bd.v == 1.5);
  BoxOf<Box<int> > bb = {{5}};
  BoxBoxPtr<int> pp = &bb;
  CHECK(pp->v.v == 5);
  IntPair<char> ip = {40, 'x'};
  CHECK(ip.a == 40 && ip.b == 'x');
  Holder<int> h = {&b};
  CHECK(h.get() == 7);
  Same<unsigned> u = 5u;
  CHECK(u == 5u);
}

int main()
{
  run();
  if (failures == 0) puts("cxx11.alias_templates ok");
  return failures != 0;
}
