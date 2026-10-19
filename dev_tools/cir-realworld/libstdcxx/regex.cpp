// Real-world TU: <regex>.
#include <regex>
#include <string>

int main() {
  std::regex re("(\\w+)@(\\w+)\\.com");
  std::smatch m;
  std::string text = "contact: alice@example.com, bob@test.com";
  int n = 0;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), re);
       it != std::sregex_iterator(); ++it)
    n += int((*it)[1].length());
  std::string r = std::regex_replace(text, re, "$2:$1");
  return n + int(r.size()) == 0;
}
