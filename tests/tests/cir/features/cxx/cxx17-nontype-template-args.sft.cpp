//type:rp
//options:--c++17:--c++17 -O2
//require:BACK_END_TYPE 3
/* cxx17.nontype_template_args: converted constant expressions as non-type template arguments: conditionals, constexpr values and calls, nullptr, references. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <int *P> struct IntPtr {
  static int get() { return P ? *P : -1; }
  static void set(int v) { *P = v; }
};
template <int (*F)(int)> struct Fn {
  static int call(int x) { return F(x); }
};
template <typename T, T *P> struct Ref {
  static T &get() { return *P; }
};
template <typename T, int T::*M> struct Member {
  static int read(const T &t) { return t.*M; }
};
template <int (&R)[4]> struct ArrRef {
  static int sum() { return R[0] + R[1] + R[2] + R[3]; }
};

int g1 = 10, g2 = 20;
int arr[4] = {1, 2, 3, 4};
struct Mp { int x, y; };
static int inc(int x) { return x + 1; }
static int dbl(int x) { return x * 2; }
constexpr bool pick_first = true;
constexpr int *pg = &g2;
constexpr int *choose(bool b) { return b ? &g1 : &g2; }

static void run()
{
  CHECK(IntPtr<&g1>::get() == 10);
  CHECK(IntPtr<(pick_first ? &g1 : &g2)>::get() == 10);
  CHECK(IntPtr<(!pick_first ? &g1 : &g2)>::get() == 20);
  CHECK(IntPtr<pg>::get() == 20);
  CHECK(IntPtr<choose(false)>::get() == 20);
  CHECK(IntPtr<choose(true)>::get() == 10);
  CHECK(IntPtr<nullptr>::get() == -1);
  IntPtr<choose(true)>::set(11);
  CHECK(g1 == 11);
  CHECK(Fn<(pick_first ? &inc : &dbl)>::call(5) == 6);
  CHECK(Fn<(!pick_first ? &inc : &dbl)>::call(5) == 10);
  CHECK((Ref<int, &g2>::get() == 20));
  Mp m = {7, 8};
  CHECK((Member<Mp, &Mp::y>::read(m) == 8));
  CHECK(ArrRef<arr>::sum() == 10);
}

int main()
{
  run();
  if (failures == 0) puts("cxx17.nontype_template_args ok");
  return failures != 0;
}
