#include "p.h"
A::~A() { printf("~A %d\n", whoami()); }
int A::whoami() { return 1; }
int A::f(int x) { return x + 1; }
A *A::self() { return this; }
B::B() : b(2) { printf("B() %d\n", whoami()); }
B::~B() { printf("~B %d\n", whoami()); }
int B::whoami() { return 2; }
int B::f(int x) { return x * 2; }
B *B::self() { return this; }
M1::M1() : m1(3) { printf("M1() %d\n", whoami()); }
M1::~M1() { printf("~M1 %d\n", whoami()); }
int M1::g() { return 30; }
int M1::whoami() { return 3; }
MI::MI() : mi(4) { printf("MI() %d %d\n", whoami(), g()); }
MI::~MI() { printf("~MI %d\n", whoami()); }
int MI::whoami() { return 4; }
int MI::g() { return 40 + m1; }
V::V() : v(5) { printf("V() %d\n", whoami()); }
V::~V() { printf("~V %d\n", whoami()); }
int V::vf() { return 50; }
int V::whoami() { return 5; }
L::L() : l(6) { printf("L() %d %d\n", whoami(), vf()); }
L::~L() { printf("~L %d\n", whoami()); }
int L::vf() { return 60 + v; }
int L::whoami() { return 6; }
R::R() : r(7) { printf("R() %d\n", whoami()); }
R::~R() { printf("~R %d\n", whoami()); }
int R::whoami() { return 7; }
D::D() : d(8) { printf("D() %d %d\n", whoami(), vf()); }
D::~D() { printf("~D %d\n", whoami()); }
int D::whoami() { return 8; }
int D::vf() { return 80 + v + l + r; }
N::N() : n(9) { printf("N() %d\n", whoami()); }
N::~N() { printf("~N %d\n", whoami()); }
int N::f(int x) { return x + n; }
E::E() : e(10) { printf("E() %d %d %d\n", whoami(), vf(), f(1)); }
E::~E() { printf("~E %d\n", whoami()); }
int E::whoami() { return 10; }
int run_all() {
  int sum = 0;
  { B b; A *pa = &b; sum += pa->f(5) + pa->whoami() + pa->self()->whoami(); }
  { MI mi; M1 *pm = &mi; A *pa = &mi; sum += pm->g() + pa->whoami() + pm->whoami(); }
  { D d; V *pv = &d; L *pl = &d; R *pr = &d; sum += pv->vf() + pv->whoami() + pl->whoami() + pr->whoami(); }
  { A *p = new E; sum += p->f(2) + p->whoami(); delete p; }
  { V *p = new D; sum += p->vf(); delete p; }
  { M1 *p = new MI; sum += p->g(); delete p; }
  return sum;
}
