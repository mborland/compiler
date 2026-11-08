//type:rp
//options:--c++11:--c++11 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* cxx11.unrestricted_unions: unions with non-trivial members: explicit construction and destruction, tagged variants. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
#include <new>

static int ctors, dtors, copies;

struct N {
  int x;
  N() : x(1) { ++ctors; }
  explicit N(int v) : x(v) { ++ctors; }
  N(const N &o) : x(o.x) { ++copies; ++ctors; }
  ~N() { ++dtors; }
};

union U {
  int i;
  N n;
  U() : i(0) {}
  ~U() {}
};

struct Variant {
  enum Tag { INT, NODE } tag;
  union {
    int i;
    N n;
  };
  Variant() : tag(INT), i(5) {}
  explicit Variant(int v) : tag(NODE) { new (&n) N(v); }
  Variant(const Variant &o) : tag(o.tag)
  {
    if (tag == NODE) new (&n) N(o.n); else i = o.i;
  }
  ~Variant() { if (tag == NODE) n.~N(); }
};

static void run()
{
  {
    U u;
    CHECK(u.i == 0 && ctors == 0);
    new (&u.n) N(41);
    CHECK(u.n.x == 41 && ctors == 1);
    u.n.~N();
    CHECK(dtors == 1);
    new (&u.n) N;
    CHECK(u.n.x == 1);
    u.n.~N();
  }
  CHECK(ctors == 2 && dtors == 2);
  {
    Variant a;
    CHECK(a.tag == Variant::INT && a.i == 5);
    Variant b(77);
    CHECK(b.tag == Variant::NODE && b.n.x == 77);
    Variant c(b);
    CHECK(c.n.x == 77 && copies == 1);
    c.n.x = 3;
    CHECK(b.n.x == 77 && c.n.x == 3);
  }
  CHECK(ctors == 4 && dtors == 4);
}

int main()
{
  run();
  if (failures == 0) puts("cxx11.unrestricted_unions ok");
  return failures != 0;
}
