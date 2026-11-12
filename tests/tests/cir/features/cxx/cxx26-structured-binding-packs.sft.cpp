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

/* Packs bound by reference (GH #525): the bindings name the argument's
   members, for reading and for writing. */
template <typename S> static long ref_total(const S &s)
{
  const auto &[...refs] = s;
  return (0L + ... + (long)refs);
}

template <typename S> static void ref_fill(S &s, int v)
{
  auto &[...r] = s;
  ((r = v), ...);
}

template <typename S> static long ref_tail_sum(S &s)
{
  auto &[h, ...tl] = s;
  long r = 0;
  ((r += tl), ...);
  h = 9;
  return r;
}

template <typename S> static long fwd_total(S &&s)
{
  auto &&[...r] = s;
  return (0L + ... + (long)r);
}

static void run_by_reference()
{
  T3 t = {1, 2, 3};
  CHECK(ref_total(t) == 6);
  CHECK(fwd_total(t) == 6);
  CHECK(ref_tail_sum(t) == 5 && t.a == 9);
  ref_fill(t, 7);
  CHECK(t.a == 7 && t.b == 7 && t.c == 7);
  int arr[4] = {10, 20, 30, 40};
  CHECK(ref_total(arr) == 100);
  CHECK(ref_tail_sum(arr) == 90 && arr[0] == 9);
  ref_fill(arr, 2);
  CHECK(ref_total(arr) == 8 && arr[3] == 2);
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
  run_by_reference();
  if (failures == 0) puts("cxx26.structured_binding_packs ok");
  return failures != 0;
}
