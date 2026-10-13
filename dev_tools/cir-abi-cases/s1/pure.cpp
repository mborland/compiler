// S1: pure virtual functions: the vtable slot holds __cxa_pure_virtual; a
// pure virtual function with a definition is still pure; the concrete
// derived class overrides.
struct A {
  virtual void f() = 0;
  virtual int g() = 0;
  virtual ~A();
  int a;
};
void A::f() {}
A::~A() {}
struct B : A {
  void f() override;
  int g() override;
};
void B::f() { A::f(); }
int B::g() { return 1; }
int use(A *p) { p->f(); return p->g(); }
B b_object;
