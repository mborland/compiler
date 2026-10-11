extern "C" int printf(const char *, ...);
struct NC { int x; int y; NC(const NC &) = delete; int sum() const { return x + y; } };
NC mk(int v) { return NC{v, v + 1}; }
NC mk2(int v) { return mk(v * 2); }
int use(int v) { NC a = mk2(v); return a.sum(); }
struct A { int a; int fa() const { return a; } };
struct B : A { int b; int fb() const { return b + fa(); } };
struct C : B { int c; int fc() const { return c + fb(); } };
int chain(const C *p) { const A *a = p; const B *b = p; return a->a + b->b + p->fc(); }
struct S { static int n; static int get() { return n; } int v; };
int S::n = 7;
int sm(S &s) { return s.get() + S::get() + s.v; }
int main() {
  C c; c.a = 1; c.b = 2; c.c = 3;
  S s; s.v = 4;
  printf("%d %d %d\n", use(5), chain(&c), sm(s));
  printf("%d\n", mk2(3).sum());
  return 0;
}
