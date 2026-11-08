//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.constexpr_in_decltype: constexpr evaluation needed by decltype: function templates instantiated in unevaluated operands (P0859R0). */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
template <typename T> constexpr unsigned long len(const T &) { return sizeof(T); }
template <unsigned long N> struct Sz { char c[N]; };
template <typename T> constexpr int tag(T x) { return (int)x + 1; }
template <int N> struct Tag { static const int value = N; };

int arr[5];
double dd[3];

decltype(Sz<len(arr)>{}) g1;
decltype(Sz<len(dd)>{}) g2;
typedef decltype(Tag<tag(2)>{}) T3;

template <typename T> struct Holder {
  decltype(Sz<len(T())>{}) data;
};

static void run()
{
  CHECK(sizeof(g1) == sizeof(arr));
  CHECK(sizeof(g2) == sizeof(dd));
  CHECK(T3::value == 3);
  Holder<long> h1;
  CHECK(sizeof(h1.data) == sizeof(long));
  Holder<char> h2;
  CHECK(sizeof(h2.data) == 1);
  int local[7];
  decltype(Sz<len(local)>{}) g3;
  CHECK(sizeof(g3) == sizeof local);
  CHECK(len(local) == sizeof local);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.constexpr_in_decltype ok");
  return failures != 0;
}
