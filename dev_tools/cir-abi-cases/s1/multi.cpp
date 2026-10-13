// S1: two polymorphic bases: the overrider of A2's virtual function needs a
// non-virtual thunk (_ZThn..), so does the destructor's.
struct A1 {
  virtual int f();
  virtual ~A1();
  int a1;
};
struct A2 {
  virtual int g();
  virtual int h();
  virtual ~A2();
  int a2;
};
struct C : A1, A2 {
  int f() override;
  int g() override;
  virtual int k();
  ~C() override;
  int c;
};
int A1::f() { return 1; }
A1::~A1() {}
int A2::g() { return 2; }
int A2::h() { return 3; }
A2::~A2() {}
int C::f() { return 4; }
int C::g() { return 5; }
int C::k() { return 6; }
C::~C() {}
int via_a2(A2 *p) { return p->g() + p->h(); }
