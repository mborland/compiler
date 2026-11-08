//type:rp
//options:--c++26 --set_flag=reflection:--c++26 --set_flag=reflection -O2
//require:BACK_END_TYPE 3
/* cxx26.reflection: reflection: splices of types, members and values; reflections as template arguments and in constant expressions. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct S {
  int a;
  long b;
  char c;
  int get() const { return a; }
};

template <auto R> struct Spliced {
  typename[:R:] value;
};
template <auto R> constexpr bool is_int = (R == ^^int);

static void run()
{
  typename[:^^int:] x = 41;
  ++x;
  CHECK(x == 42);
  typename[:^^S:] s = {1, 2, 'z'};
  CHECK(s.a == 1 && s.b == 2 && s.c == 'z');
  typename[:^^long:] big = 1L << 40;
  CHECK(big == (1L << 40) && sizeof big == sizeof(long));
  CHECK(s.[:^^S::a:] == 1);
  s.[:^^S::b:] = 9;
  CHECK(s.b == 9);
  Spliced<^^short> sp = {7};
  CHECK(sp.value == 7 && sizeof sp.value == sizeof(short));
  Spliced<^^S> ss = {{3, 4, 'q'}};
  CHECK(ss.value.b == 4);
  static_assert(is_int<^^int>, "int");
  static_assert(!is_int<^^long>, "long");
  CHECK(is_int<^^int> && !is_int<^^S>);
  CHECK((s.*&S::get)() == 1);
  CHECK(s.[:^^S::get:]() == 1);
}

int main()
{
  run();
  if (failures == 0) puts("cxx26.reflection ok");
  return failures != 0;
}
