// S1: single inheritance, a key function (the first non-inline virtual
// function: f), a virtual destructor defined inline, overriding.
struct A {
  virtual int f(int x);
  virtual int g() { return 2; }
  virtual ~A() {}
  int a;
};
struct B : A {
  int f(int x) override;
  virtual void h();
  int b;
};
int A::f(int x) { return x; }
int B::f(int x) { return x + 1; }
void B::h() {}
int call(A *p) { return p->f(1) + p->g(); }
int call_b(B *p) { p->h(); return p->f(2); }
