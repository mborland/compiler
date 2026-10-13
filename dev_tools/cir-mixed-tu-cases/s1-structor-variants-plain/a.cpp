#include "common.h"
#include <cstdio>

A::A() : a(1) { std::printf("A()\n"); }
A::~A() { std::printf("~A\n"); }
int sum(A *p) { return p->a * 100; }
