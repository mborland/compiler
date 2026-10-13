#include "common.h"
#include <cstdio>

Tracer::~Tracer() { std::printf("~%s\n", name); }
A::~A() { std::printf("~A\n"); }
void destroy(A *p) { delete p; }
