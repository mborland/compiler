#include <cstdio>
int f4(); int f5(); int f6(int);
int main() { std::printf("f4=%d\n", f4()); std::printf("f5=%d\n", f5()); std::printf("f6(1)=%d\n", f6(1)); std::printf("f6(2)=%d\n", f6(2)); return 0; }
