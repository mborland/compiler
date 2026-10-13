// S2: a diamond with a virtual base: A is a virtual base of B and C; D has
// two vptrs plus A's, a VTT (_ZTT1D), construction vtables for B-in-D and
// C-in-D (_ZTC1D0_1B, _ZTC1D8_1C), and the C1/C2/D0/D1/D2 variants of the
// structors (the base-object variants take the VTT parameter).
struct A {
  A();
  virtual int f();
  virtual ~A();
  int a;
};
struct B : virtual A {
  B();
  int f() override;
  ~B() override;
  int b;
};
struct C : virtual A {
  C();
  virtual int g();
  ~C() override;
  int c;
};
struct D : B, C {
  D();
  int f() override;
  int g() override;
  ~D() override;
  int d;
};
A::A() : a(1) {}
int A::f() { return a; }
A::~A() {}
B::B() : b(2) {}
int B::f() { return b; }
B::~B() {}
C::C() : c(3) {}
int C::g() { return c; }
C::~C() {}
D::D() : d(4) {}
int D::f() { return d; }
int D::g() { return d + 1; }
D::~D() {}
int use(A *a, B *b, C *c) { return a->f() + b->f() + c->g(); }
D global_d;
