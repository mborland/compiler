// S1: virtual destructors: D0 (deleting), D1 (complete), D2 (base), the
// deleting destructor called by delete through a base pointer, and the
// destructor of a derived class calling its base's D2.
struct A {
  virtual ~A();
  int a;
};
struct B : A {
  ~B() override;
  int *p;
};
A::~A() {}
B::~B() { delete p; }
void del(A *a) { delete a; }
void del_b(B *b) { delete b; }
void local() { B b; (void)b; }
