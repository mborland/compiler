// Real-world TU: Boost.CircularBuffer and Boost.Optional.
#include <boost/circular_buffer.hpp>
#include <boost/optional.hpp>

int main() {
  boost::circular_buffer<int> cb(3);
  for (int i = 0; i < 5; ++i) cb.push_back(i);
  boost::optional<int> o = cb.front();
  return int(cb.size() + *o) == 0;
}
