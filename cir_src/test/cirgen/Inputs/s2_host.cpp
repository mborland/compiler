// The host side of cxx-s2-twotu.test: classes whose bases (with virtual
// bases, vptrs and virtual functions) are compiled by the CIR back end, and
// classes derived from them here, so that this compiler's constructors call
// the library's base-variant constructors with a VTT of its own, and the
// library's code reads the vtables and VTTs this compiler writes.
struct V { int v; V(); virtual ~V(); virtual int f(); virtual int h(int); };
struct L : virtual V { int l; L(); ~L(); virtual int g(); int h(int) override; };
struct R : virtual V { int r; R(); ~R(); int f() override; };
struct X : L, R { int x; X(); ~X(); int g() override; int f() override; };
extern int g_d;
int use_v(V *p);
int use_l(L *p);
int use_x(X *p);
V *make_x();

// A diamond whose vtables, VTT and construction vtables are ours.
struct H : L, R {
  int hh = 7;
  int g() override { return 5000 + v; }
  int f() override { return 6000 + v; }
  ~H() override { g_d += 100000; }
};
// A class that derives from a library class with virtual bases.
struct K : X {
  int k = 9;
  int f() override { return 7000 + v; }
  ~K() override { g_d += 1000000; }
};
// A new class that shares V virtually with the library's classes.
struct W : virtual V {
  int w = 3;
  int f() override { return 8000 + v; }
};
struct Z : W, L {
  int z = 4;
  int f() override { return 9000 + v; }
};

int main()
{
  {
    X x;
    if (x.v != 1 || x.l != 2 || x.r != 3 || x.x != 4)
      return 1;
    V *p = &x;
    if (p->f() != 2001 || p->h(1) != 3)
      return 2;
    L *pl = &x;
    if (pl->g() != 1001)
      return 3;
  }
  if (g_d != 1111)
    return 4;
  g_d = 0;
  {
    H h;
    if (h.v != 1 || h.l != 2 || h.r != 3 || h.hh != 7)
      return 5;
    V *p = &h; L *pl = &h; R *pr = &h;
    if (use_v(p) != 6001 + 3 || use_l(pl) != 5001 + 1 + 2)
      return 6;
    if (pr->f() != 6001 || pl->g() != 5001)
      return 7;
  }
  if (g_d != 100111)
    return 9;
  g_d = 0;
  {
    K k;
    X *px = &k;
    if (use_x(px) != (7001 + 3) + (1001 + 1 + 2) + 3 + 4)
      return 10;
    if (k.k != 9 || k.x != 4 || k.v != 1)
      return 11;
    V *p = &k;
    if (p->f() != 7001)
      return 12;
  }
  if (g_d != 1001111)
    return 13;
  g_d = 0;
  {
    Z z;
    V *p = &z; L *pl = &z; W *pw = &z;
    if (p->f() != 9001 || pw->f() != 9001 || pl->g() != 11)
      return 14;
    if (z.w != 3 || z.z != 4 || z.l != 2)
      return 15;
  }
  if (g_d != 11)
    return 16;
  g_d = 0;
  V *q = make_x();
  if (q->f() != 2001)
    return 17;
  delete q;
  if (g_d != 1111)
    return 18;
  return 0;
}
