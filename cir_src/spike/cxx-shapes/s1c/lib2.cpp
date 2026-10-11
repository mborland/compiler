#include <cstdio>
#include "a.h"
A::A() : x(1) { std::printf("A() %p\n", (void*)this); }
A::A(int v) : x(v) { std::printf("A(int) %p\n", (void*)this); }
A::A(const A &o) : x(o.x) { std::printf("A(const A&) %p <- %p\n", (void*)this, (const void*)&o); }
A::~A() { std::printf("~A() %p\n", (void*)this); }
A make() { return A(5); }
