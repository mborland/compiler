// Kernel: std::sort of a pseudo-random array of ints, then a checksum.
#include <algorithm>
#include <cstdio>
#include <vector>

int main() {
  std::vector<unsigned> v(400000);
  unsigned x = 12345;
  for (auto &e : v) {
    x = x * 1664525u + 1013904223u;
    e = x >> 8;
  }
  std::sort(v.begin(), v.end());
  unsigned long sum = 0;
  for (std::size_t i = 0; i < v.size(); i += 97)
    sum += v[i] * (i + 1);
  std::printf("%lu\n", sum);
  return 0;
}
