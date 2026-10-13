//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: multiple inheritance of polymorphic classes.  Z has a primary
// base (X) and a non-primary base (Y); W derives from Z.  The override lists
// are per base subobject (CLASS_OVERRIDES), conversions to an indirect base
// are chains of BASE_CAST, a downcast is a chain of DERIVED_CAST, and a
// class derived from a polymorphic class has its layout facts at once.
extern "C" int printf(const char *, ...);
struct X {
  int x;
  virtual void g() { printf("X::g\n"); }
  virtual void h() { printf("X::h\n"); }
  virtual ~X() {}
};
struct Y {
  int y;
  virtual void h() { printf("Y::h\n"); }
  virtual int k(int a) const { return a; }
  virtual ~Y() {}
};
struct Z : X, Y {
  int z;
  Z() : z(4) {}
  void g() override { printf("Z::g\n"); }
  void h() override { printf("Z::h\n"); }
  int k(int a) const override { return a + z; }
  virtual void zz() { printf("Z::zz\n"); }
};
struct W : Z {
  void zz() override { printf("W::zz\n"); }
};
struct Plain { int p; };
struct PD : Plain { virtual void f() {} };
void use(X *px, Y *py, Z *pz, W *pw) {
  px->g();
  py->h();
  pz->h();
  pw->zz();
  Y *y2 = pw;
  X *x2 = pw;
  Z *z2 = static_cast<Z *>(px);
  W *w2 = static_cast<W *>(py);
  const Y *cy = pw;
  printf("%d\n", cy->k(1) + (y2 != 0) + (x2 != 0) + (z2 != 0) + (w2 != 0));
}
void destroy(X *px, Y *py) {
  delete px;
  delete py;
}
int main() {
  Z z;
  W w;
  PD pd;
  use(&z, &z, &z, &w);
  destroy(new Z, new W);
  return 0;
}
