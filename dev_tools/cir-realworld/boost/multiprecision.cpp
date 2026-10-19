// Real-world TU: Boost.Multiprecision cpp_int.
#include <boost/multiprecision/cpp_int.hpp>

int main() {
  using boost::multiprecision::cpp_int;
  cpp_int a = 1;
  for (int i = 1; i <= 30; ++i) a *= i;
  cpp_int b = a / 12345 + a % 777;
  return int(b > 0) == 0;
}
