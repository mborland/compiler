#include <cstdio>
int f1(); int f2(bool); int f3();
int main() { std::printf("f1=%d\n", f1()); std::printf("f2(true)=%d\n", f2(true)); std::printf("f2(false)=%d\n", f2(false)); std::printf("f3=%d\n", f3()); return 0; }
