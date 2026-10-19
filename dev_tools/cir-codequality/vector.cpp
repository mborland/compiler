// Kernel: std::vector growth and the algorithms over it.
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <vector>

int main() {
  std::vector<long> v;
  for (long i = 0; i < 1500000; ++i) v.push_back(i * 3 % 1009);
  std::vector<long> w(v.size());
  std::transform(v.begin(), v.end(), w.begin(), [](long x) { return x * x; });
  long total = std::accumulate(w.begin(), w.end(), 0L);
  long evens = std::count_if(v.begin(), v.end(), [](long x) { return x % 2 == 0; });
  std::printf("%ld %ld\n", total, evens);
  return 0;
}
