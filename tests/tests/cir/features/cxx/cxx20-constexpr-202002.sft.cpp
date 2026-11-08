//type:rp
//options:--c++20:--c++20 -O2
//require:BACK_END_TYPE 3
/* cxx20.constexpr_202002: constexpr: trivial default initialization, changing the active union member, dropped restrictions. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
union Num {
  int i;
  double d;
  constexpr Num() : i(0) {}
};

constexpr double flip()
{
  Num n;
  n.i = 3;
  int a = n.i;
  n.d = 1.5;
  return a + n.d;
}
static_assert(flip() == 4.5, "active member");

constexpr int uninit(int n)
{
  int arr[4];
  int x;
  x = n;
  for (int i = 0; i < 4; ++i) arr[i] = x + i;
  return arr[0] + arr[3];
}
static_assert(uninit(5) == 5 + 8, "trivial default init");

struct Pt {
  int x, y;
};
constexpr int pt_sum()
{
  Pt p;
  p.x = 3;
  p.y = 4;
  return p.x + p.y;
}
static_assert(pt_sum() == 7, "uninitialized aggregate");

constexpr int try_in(int a)
{
  try {
    return a + 1;
  } catch (int) {
    return -1;
  }
}
static_assert(try_in(1) == 2, "try");

constexpr int *addr_of(int &r) { return &r; }
constexpr int deref_chain()
{
  int v = 5;
  int *p = addr_of(v);
  *p += 3;
  return v;
}
static_assert(deref_chain() == 8, "pointer");

static void run()
{
  int n = 7;
  CHECK(flip() == 4.5);
  CHECK(uninit(n) == 7 + 10);
  CHECK(pt_sum() == 7);
  CHECK(try_in(n) == 8);
  CHECK(deref_chain() == 8);
  Num u;
  u.d = 2.5;
  CHECK(u.d == 2.5);
  u.i = 9;
  CHECK(u.i == 9);
}

int main()
{
  run();
  if (failures == 0) puts("cxx20.constexpr_202002 ok");
  return failures != 0;
}
