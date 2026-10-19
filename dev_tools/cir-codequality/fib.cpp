// Kernel: recursive calls (call overhead and frame setup).
#include <cstdio>

static long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main() {
  std::printf("%ld\n", fib(33));
  return 0;
}
