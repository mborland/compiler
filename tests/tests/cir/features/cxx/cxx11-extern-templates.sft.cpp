//type:rp
//options:--c++11:--c++11 -O2
//require:BACK_END_TYPE 3
/* cxx11.extern_templates: extern template declarations suppress implicit instantiation; the explicit instantiation
   definition later in the unit provides the code. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> struct Foo {
  T v;
  T get() const;
  static int count;
  template <typename U> U conv() const { return (U)v; }
};
template <typename T> T Foo<T>::get() const { return v + 1; }
template <typename T> int Foo<T>::count = 7;

template <typename T> T maxof(T a, T b) { return a > b ? a : b; }
template <typename T> int sizeofT() { return (int)sizeof(T); }

extern template struct Foo<int>;
extern template int maxof<int>(int, int);
extern template double maxof<double>(double, double);
extern template int sizeofT<long>();

static void run()
{
  Foo<int> f = {41};
  CHECK(f.get() == 42);
  CHECK(Foo<int>::count == 7);
  Foo<int>::count = 8;
  CHECK(Foo<int>::count == 8);
  CHECK(maxof(3, 9) == 9);
  CHECK(maxof(2.5, 1.5) == 2.5);
  CHECK(sizeofT<long>() == (int)sizeof(long));
  CHECK(f.conv<long>() == 41L);
  Foo<char> c = {'a'};
  CHECK(c.get() == 'b');
}

template struct Foo<int>;
template int maxof<int>(int, int);
template double maxof<double>(double, double);
template int sizeofT<long>();

int main()
{
  run();
  if (failures == 0) puts("cxx11.extern_templates ok");
  return failures != 0;
}
