//options:--c++17
// (the S2 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S2: the diamond.  V is a virtual base of L and R; D derives from
// both.  The record of D has a VIRTUAL_BASE field for V after its data members
// (at its complete-object offset), STRUCTOR has_vbases is set for L, R and D,
// C1 has the MEMINIT(vbase) group and C2 does not, D1 is synthesized from the
// virtual bases' cleanups and a call of D2 with VTT_ADDR(self), the bases are
// constructed with C2/D2 and the VTT of the class being constructed.  The vtable
// is followed by its VTT.
extern "C" int printf(const char *, ...);
struct V {
  int v;
  V(int x) : v(x) { printf("V(%d)\n", x); }
  virtual int f() { return v; }
  virtual ~V() { printf("~V\n"); }
};
struct L : virtual V {
  int l;
  L() : V(1), l(2) { printf("L()\n"); }
  int f() override { return l + v; }
  ~L() override { printf("~L\n"); }
};
struct R : virtual V {
  int r;
  R() : V(3), r(4) { printf("R()\n"); }
  ~R() override { printf("~R\n"); }
};
struct D : L, R {
  int d;
  D() : V(5), d(6) { printf("D()\n"); }
  int f() override { return d + l + r + v; }
  ~D() override { printf("~D\n"); }
};
int use(V *v, L *l, R *r, D *d) { return v->f() + l->f() + r->v + d->v; }
int main() {
  D d;
  L l;
  R *pr = new D;
  int n = use(&d, &d, &d, &d) + use(&l, &l, pr, &d);
  delete pr;
  return n == 0;
}
