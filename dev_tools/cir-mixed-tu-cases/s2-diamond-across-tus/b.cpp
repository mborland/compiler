#include "common.h"
#include <cstdio>

D::D() { std::printf("D() f=%d g=%d\n", f(), g()); }
int D::f() { return 40 + d + a + b; }
int D::g() { return 50 + d + a + c; }
D::~D() { std::printf("~D\n"); }
D *make_d() { return new D; }
