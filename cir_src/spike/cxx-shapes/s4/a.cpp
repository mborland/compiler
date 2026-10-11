#include <cstdio>
struct P { long a, b; };
struct A { long s; A(P p); ~A(); };
A::A(P p) : s(p.a + p.b) { std::printf("A(P) %p s=%ld\n", (void*)this, s); }
A::~A() { std::printf("~A %p\n", (void*)this); }
