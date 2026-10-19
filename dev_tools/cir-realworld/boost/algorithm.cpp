// Real-world TU: Boost.Algorithm string functions.
#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>

int main() {
  std::string s = "  Hello, World  ";
  boost::trim(s);
  boost::to_upper(s);
  std::vector<std::string> parts;
  boost::split(parts, s, boost::is_any_of(", "), boost::token_compress_on);
  boost::replace_all(s, "WORLD", "there");
  return int(parts.size() + s.size()) == 0;
}
