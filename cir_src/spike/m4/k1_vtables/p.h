extern "C" int printf(const char *, ...);
// Single, multiple and virtual inheritance, with virtual calls in
// constructors/destructors (construction vtables), covariant returns, VTT.
struct A {
  int a;
  A() : a(1) { printf("A() %d\n", whoami()); }
  virtual ~A();
  virtual int whoami();
  virtual int f(int x);
  virtual A *self();
};
struct B : A {
  int b;
  B();
  ~B() override;
  int whoami() override;
  int f(int x) override;
  B *self() override;
};
struct M1 { int m1; M1(); virtual ~M1(); virtual int g(); virtual int whoami(); };
struct MI : B, M1 {
  int mi;
  MI();
  ~MI() override;
  int whoami() override;
  int g() override;
};
// diamond with virtual bases
struct V { int v; V(); virtual ~V(); virtual int vf(); virtual int whoami(); };
struct L : virtual V { int l; L(); ~L() override; int vf() override; int whoami() override; };
struct R : virtual V { int r; R(); ~R() override; int whoami() override; };
struct D : L, R { int d; D(); ~D() override; int whoami() override; int vf() override; };
struct N : virtual A { int n; N(); ~N() override; int f(int) override; };
struct E : D, N { int e; E(); ~E() override; int whoami() override; };
int run_all();
