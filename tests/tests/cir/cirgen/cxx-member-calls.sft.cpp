//type:rp
//options:--c++17
//require:BACK_END_TYPE 3
// Member functions: const and non-const, this-returning chains, calls through
// base classes and base pointers, a member function that returns a class by
// value, and class-typed members.
extern "C" int printf(const char *, ...);
struct A { int a; int fa() const { return a; } A &inc() { ++a; return *this; } };
struct B : A { int b; int fb() const { return b + fa(); } };
struct C : B { int c; int fc() const { return c + fb(); } };
struct V { int p; int q; V swapped() const { return V{q, p}; } int diff() const { return p - q; } };
struct Holder { V v; A a; int total() const { return v.p + v.q + a.a; } };
int chain(const C *p) { const A *a = p; const B *b = p; return a->a + b->b + p->fc(); }
int main() {
  C c;
  c.a = 1; c.b = 2; c.c = 3;
  int bad = 0;
  if (chain(&c) != 1 + 2 + 6) bad |= 1;
  c.inc().inc();
  if (c.a != 3 || c.fc() != 8) bad |= 2;
  V v{9, 4};
  if (v.swapped().diff() != -5 || v.diff() != 5) bad |= 4;
  Holder h{{1, 2}, {3}};
  h.a.inc();
  if (h.total() != 7) bad |= 8;
  printf("%d %d %d bad=%d\n", chain(&c), v.swapped().p, h.total(), bad);
  return bad;
}
