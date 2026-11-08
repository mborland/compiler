//type:rp
//options:--c++03:--c++03 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* cxx98.nothrow_new: new (std::nothrow) returns null instead of throwing; a class-specific nothrow operator new
   that returns null skips the constructor; a throwing constructor calls the matching placement delete. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
#include <new>

static int ctor_calls;
static int placement_deletes;
static int plain_deletes;

struct Null {
  int v;
  Null() : v(1) { ++ctor_calls; }
  static void *operator new(std::size_t, const std::nothrow_t &) throw()
  { return 0; }
  static void *operator new(std::size_t n) { return ::operator new(n); }
  static void operator delete(void *p) { ::operator delete(p); }
  static void operator delete(void *p, const std::nothrow_t &) throw()
  { ::operator delete(p); }
};

struct Thrower {
  int v;
  Thrower() : v(2) { throw 5; }
  static void *operator new(std::size_t n, const std::nothrow_t &) throw()
  { return ::operator new(n); }
  static void operator delete(void *p, const std::nothrow_t &) throw()
  { ++placement_deletes; ::operator delete(p); }
  static void operator delete(void *p) { ++plain_deletes; ::operator delete(p); }
};

struct Plain {
  int v;
  Plain() : v(9) { ++ctor_calls; }
};

static char *volatile slot;
static volatile std::size_t huge = ~(std::size_t)0 / 2;

static void run()
{
  char *big = new (std::nothrow) char[huge];
  slot = big;
  CHECK(big == 0);
  int *ip = new (std::nothrow) int[16];
  CHECK(ip != 0);
  for (int i = 0; i < 16; ++i) ip[i] = i;
  int s = 0;
  for (int i = 0; i < 16; ++i) s += ip[i];
  CHECK(s == 120);
  delete[] ip;

  Plain *pl = new (std::nothrow) Plain;
  CHECK(pl != 0 && pl->v == 9 && ctor_calls == 1);
  delete pl;

  Null *n = new (std::nothrow) Null;
  CHECK(n == 0);
  CHECK(ctor_calls == 1);

  int caught = 0;
  try {
    Thrower *t = new (std::nothrow) Thrower;
    (void)t;
  } catch (int e) {
    caught = e;
  }
  CHECK(caught == 5);
  CHECK(placement_deletes == 1 && plain_deletes == 0);
}

int main()
{
  run();
  if (failures == 0) puts("cxx98.nothrow_new ok");
  return failures != 0;
}
