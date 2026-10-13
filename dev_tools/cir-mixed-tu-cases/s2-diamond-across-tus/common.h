/* A diamond built across translation units: A is a virtual base of B and C;
   a.cpp defines A, B and C (their constructors, destructors and virtual
   functions) and b.cpp defines D : B, C.  D's constructor passes the VTT of
   the complete object to the base-object constructors of B and C in a.cpp
   (which read their vptrs from it), and D's destructor does the same for
   the destructors.  The calls through A *, B * and C * need the vbase
   offsets and the vcall offsets in D's vtable. */
struct A {
  A();
  virtual int f();
  virtual ~A();
  int a = 1;
};
struct B : virtual A {
  B();
  int f() override;
  ~B() override;
  int b = 2;
};
struct C : virtual A {
  C();
  virtual int g();
  ~C() override;
  int c = 3;
};
struct D : B, C {
  D();
  int f() override;
  int g() override;
  ~D() override;
  int d = 4;
};
D *make_d();                  // b.cpp
void destroy(A *p);           // a.cpp: delete p
int use_b(B *p);              // a.cpp: p->f() + p->b
int use_c(C *p);              // a.cpp: p->g() + p->c
