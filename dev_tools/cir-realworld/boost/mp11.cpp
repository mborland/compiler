// Real-world TU: Boost.Mp11 template metaprogramming.
#include <boost/mp11.hpp>
#include <tuple>

using namespace boost::mp11;
using L = mp_list<int, char, long>;
static_assert(mp_size<L>::value == 3);
static_assert(std::is_same<mp_at_c<L, 1>, char>::value);

int main() {
  std::tuple<int, char, long> t{1, 'a', 3};
  long sum = 0;
  tuple_for_each(t, [&](auto x) { sum += x; });
  return sum == 0;
}
