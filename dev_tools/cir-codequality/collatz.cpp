// Kernel: a hot integer loop with data-dependent branches.
#include <cstdio>

int main() {
  unsigned long best = 0, besti = 0;
  for (unsigned long i = 1; i < 300000; ++i) {
    unsigned long n = i, steps = 0;
    while (n != 1) {
      n = (n & 1) ? 3 * n + 1 : n / 2;
      ++steps;
    }
    if (steps > best) { best = steps; besti = i; }
  }
  std::printf("%lu %lu\n", besti, best);
  return 0;
}
