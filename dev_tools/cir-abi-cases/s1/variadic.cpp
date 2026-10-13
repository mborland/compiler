// S1: a virtual function with a variadic parameter list in the second base:
// its overrider needs a thunk that forwards the variable arguments.
struct A1 {
  virtual void f();
  int a1;
};
struct A2 {
  virtual int g(int n, ...);
  virtual int h(double d);
  int a2;
};
struct C : A1, A2 {
  int g(int n, ...) override;
  int h(double d) override;
};
void A1::f() {}
int A2::g(int n, ...) { return n; }
int A2::h(double d) { return (int)d; }
int C::g(int n, ...) { return n + 1; }
int C::h(double d) { return (int)d + 1; }
int use(A2 *p) { return p->g(1, 2, 3) + p->h(2.5); }
