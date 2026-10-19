// Kernel: std::unordered_map inserts, lookups and erases.
#include <cstdio>
#include <unordered_map>

int main() {
  std::unordered_map<unsigned, unsigned> m;
  unsigned x = 7;
  for (unsigned i = 0; i < 200000; ++i) {
    x = x * 1103515245u + 12345u;
    m[x & 0xfffff] += i;
  }
  unsigned long sum = 0;
  for (unsigned i = 0; i < 200000; ++i) {
    auto it = m.find(i);
    if (it != m.end()) sum += it->second;
  }
  for (unsigned i = 0; i < 100000; ++i) m.erase(i);
  std::printf("%lu %zu\n", sum, m.size());
  return 0;
}
