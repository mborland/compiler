//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Copy elision with address identity: a class with user-provided constructors
// (passed and returned indirectly) records the address each constructor ran
// at; the object that ends up in the caller must be the one that was
// constructed (prvalue returns, chains, temporaries bound to by-value
// parameters, ?: arms), and a named return value (NRVO) is built in the
// caller's object; copies are counted.
extern "C" int printf(const char *, ...);
void *g_this;
int g_copies;
struct A {
  int v;
  A(int x);
  A(const A &o);
  ~A() { v = -1; }
};
A::A(int x) { g_this = this; v = x; }
A::A(const A &o) { ++g_copies; g_this = this; v = o.v; }
A make(int x) { return A(x); }
A chain(int x) { return make(x); }
A named(int x) { A a(x); return a; }
A named2(int x) { A a(x); if (x > 100) return A(0); return a; }
void *g_param;
int take(A a) { g_param = &a; return a.v; }
int bad = 0;
void check(int ok, int code) { if (!ok) bad = bad * 10 + code; }
int main() {
  A a = make(5);
  check(g_this == &a && a.v == 5, 1);
  A b = chain(6);
  check(g_this == &b && b.v == 6, 2);
  check(take(A(8)) == 8, 3);
  check(g_param == g_this, 4);
  check(take(make(9)) == 9, 5);
  check(g_param == g_this, 6);
  A c = (g_copies == 0) ? make(10) : make(11);
  check(g_this == &c && c.v == 10, 7);
  check(g_copies == 0, 8);
  A d = named(12);
  check(g_this == &d && d.v == 12 && g_copies == 0, 9);
  printf("%d %d %d %d copies=%d bad=%d\n", a.v, b.v, c.v, d.v, g_copies, bad);
  return bad != 0;
}
