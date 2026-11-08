//type:rp
//options:--c++26:--c++26 -O2
//require:BACK_END_TYPE 3
/* cxx26.structured_binding_packs: structured binding packs (P1061R10), declared in templates. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
struct T3 { int a; long b; char c; };

template <typename S> static int count(const S &s)
{
  auto [...parts] = s;
  return (int)sizeof...(parts);
}

template <typename S> static long total(const S &s)
{
  auto [...parts] = s;
  return (0L + ... + (long)parts);
}

template <typename S> static long tail_sum(const S &s)
{
  auto [h, ...tl] = s;
  long r = 0;
  ((r += tl), ...);
  return r * 100 + h;
}

template <typename S> static int arr_first(const S &s)
{
  auto [first, ...rest] = s;
  return first * 10 + (int)sizeof...(rest);
}

static void run()
{
  T3 t = {1, 2, 3};
  CHECK(count(t) == 3);
  CHECK(total(t) == 6);
  int arr[4] = {10, 20, 30, 40};
  CHECK(count(arr) == 4 && total(arr) == 100);
  CHECK(tail_sum(arr) == 90 * 100 + 10);
  CHECK(tail_sum(t) == 5 * 100 + 1);
  CHECK(arr_first(arr) == 103);
}

int main()
{
  run();
  if (failures == 0) puts("cxx26.structured_binding_packs ok");
  return failures != 0;
}
