#include "common.h"

int B::f() { return 3; }
int D::g() { return 4; }
A *make_b() { return new B; }
C *make_d_as_c() { return new D; }
