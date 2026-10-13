#include "common.h"
#include <cstdio>

void A::f() { std::printf("A::f\n"); }
A::~A() {}
int run(A *p) { p->f(); return p->g(); }
