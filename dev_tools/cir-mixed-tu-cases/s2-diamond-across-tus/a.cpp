#include "common.h"
#include <cstdio>

A::A() { std::printf("A()\n"); }
int A::f() { return 10 + a; }
A::~A() { std::printf("~A\n"); }
B::B() { std::printf("B() f=%d\n", f()); }
int B::f() { return 20 + b + a; }
B::~B() { std::printf("~B\n"); }
C::C() { std::printf("C() g=%d\n", g()); }
int C::g() { return 30 + c + a; }
C::~C() { std::printf("~C\n"); }
void destroy(A *p) { delete p; }
int use_b(B *p) { return p->f() + p->b; }
int use_c(C *p) { return p->g() + p->c; }
