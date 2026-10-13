/* The base-object structors: A's constructor and destructor are external
   and not inline (a.cpp), and B : A (b.cpp) calls A's base-object variants
   (_ZN1AC2Ev and _ZN1AD2Ev) from its own constructor and destructor, and
   its complete-object variants (C1, D1) call B's.  Either translation unit
   can be compiled by cpfe-cir: the other one must find every variant it
   refers to (g++ calls C2 and D2, clang does too).  The deleting destructor
   (D0) is reached by delete through a base pointer. */
struct A {
  A();
  virtual int f();
  virtual ~A();
  int a;
};
struct B : A {
  B();
  int f() override;
  ~B() override;
  int b;
};
A *make_b();          // b.cpp
void destroy(A *p);   // a.cpp: delete p
int use(A *p);        // a.cpp: p->f()
