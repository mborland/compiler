// Real-world TU: Boost.Container.
#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

int main() {
  boost::container::flat_map<int, int> m;
  m[1] = 2; m[3] = 4;
  boost::container::small_vector<int, 8> sv{1, 2, 3};
  boost::container::static_vector<int, 4> st{1, 2};
  sv.push_back(4); st.push_back(3);
  return int(m.size() + sv.size() + st.size()) == 0;
}
