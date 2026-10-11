#include <cstdio>
int f(int n) { std::printf("f(%d)\n", n); return n; }
struct S { int id; S(int i); ~S(); };
S::S(int i) : id(i) { std::printf("S(%d)\n", i); }
S::~S() { std::printf("~S(%d)\n", id); }
