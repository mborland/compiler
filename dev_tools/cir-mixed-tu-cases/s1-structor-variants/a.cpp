#include "common.h"
#include <cstdio>

A::A() : a(1) { std::printf("A()\n"); }
int A::f() { return a; }
A::~A() { std::printf("~A\n"); }
void destroy(A *p) { delete p; }
int use(A *p) { return p->f(); }
