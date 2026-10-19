// Real-world TU: Boost.Range adaptors.
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/algorithm.hpp>
#include <vector>

int main() {
  std::vector<int> v{1, 2, 3, 4, 5, 6};
  int sum = 0;
  for (int x : v | boost::adaptors::filtered([](int i) { return i % 2 == 0; })
                 | boost::adaptors::transformed([](int i) { return i * i; }))
    sum += x;
  boost::sort(v);
  return sum == 0;
}
