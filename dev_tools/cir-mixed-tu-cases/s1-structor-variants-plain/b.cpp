#include "common.h"
#include <cstdio>

B::B() : b(2) { std::printf("B()\n"); }
B::~B() { std::printf("~B\n"); }
B *make_b() { return new B; }
void destroy(B *p) { delete p; }
