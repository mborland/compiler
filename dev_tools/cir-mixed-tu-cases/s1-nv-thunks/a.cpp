#include "common.h"

int A1::f() { return 1; }
A1::~A1() {}
int A2::g(int x) { return x; }
int A2::h(int x, double y) { return x + (int)y; }
A2::~A2() {}
int call_a2(A2 *p) { return p->g(5) * 1000 + p->h(6, 7.5); }
