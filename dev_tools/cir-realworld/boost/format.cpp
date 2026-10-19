// Real-world TU: Boost.Format.
#include <boost/format.hpp>
#include <string>

int main() {
  std::string s = (boost::format("%1% + %2% = %3%") % 1 % 2 % 3).str();
  return int(s.size()) == 0;
}
