#include <cstdio>
struct P { long a, b; };
struct A { long s; A(P p); ~A(); };
int main() { P p{3, 4}; A a(p); std::printf("s=%ld\n", a.s); return 0; }
