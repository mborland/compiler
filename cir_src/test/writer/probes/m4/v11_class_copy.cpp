// M4a probe: copies and loads of records with a vptr or virtual bases: which
// init kinds the FE uses (dik_constructor vs bitwise), slicing, by-value
// arguments and returns, assignment.
#include <cstdio>
struct B { int x; virtual void f() { std::puts("B::f"); } B() : x(1) {} };
struct D : B { int y; void f() { std::puts("D::f"); } D() : y(2) {} };
struct V { int v; V() : v(3) {} };
struct W : virtual V { int w; W() : w(4) {} };
B copy1(B b) { return b; }                  // by-value parameter and return
void sliceinit(const D &d) { B b = d; B c(b); b = c; b = d; (void)b; }
W copyw(const W &w) { W a = w; W b; b = a; return b; }
int main() { D d; sliceinit(d); B r = copy1(d); W w; W w2 = copyw(w); return r.x + w2.v - 4; }
