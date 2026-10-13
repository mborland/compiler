#include "common.h"

int A::f() { return 100 + a; }
A::~A() {}
A *make_a() { return new A; }
int sum_a(A *p) { return p->f() + p->g(); }
