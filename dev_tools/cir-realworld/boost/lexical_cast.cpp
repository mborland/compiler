// Real-world TU: Boost.LexicalCast.
#include <boost/lexical_cast.hpp>
#include <string>

int main() {
  int a = boost::lexical_cast<int>("1234");
  double b = boost::lexical_cast<double>("2.5");
  std::string c = boost::lexical_cast<std::string>(a + b);
  return int(c.size()) == 0;
}
