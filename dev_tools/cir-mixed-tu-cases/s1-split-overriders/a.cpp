#include "common.h"

int A1::f() { return 1; }
int A2::g(int x) { return x; }
int C::f() { return 100 + c; }
A2 *make_c() { return new C; }
A1 *make_c_as_a1() { return new C; }
int call_g(A2 *p) { return p->g(5); }
int call_f(A1 *p) { return p->f(); }
