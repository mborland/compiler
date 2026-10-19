// Real-world TU: <algorithm> and <numeric> over a vector.
#include <algorithm>
#include <numeric>
#include <vector>

int main() {
  std::vector<int> v{5, 3, 1, 4, 2};
  std::sort(v.begin(), v.end());
  std::stable_sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
  auto it = std::lower_bound(v.rbegin(), v.rend(), 3);
  std::vector<int> w(v.size());
  std::partial_sum(v.begin(), v.end(), w.begin());
  std::rotate(v.begin(), v.begin() + 2, v.end());
  std::nth_element(v.begin(), v.begin() + 2, v.end());
  std::reverse(w.begin(), w.end());
  auto mm = std::minmax_element(v.begin(), v.end());
  std::unique(w.begin(), w.end());
  return int(*it + *mm.first + std::accumulate(v.begin(), v.end(), 0)) == 0;
}
