#include "common.h"
#include <cstdio>

B::B() : b(2) { std::printf("B()\n"); }
int B::f() { return 10 + b + a; }
B::~B() { std::printf("~B\n"); }
A *make_b() { return new B; }
