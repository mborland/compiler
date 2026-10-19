// Real-world TU: <functional>, std::function, bind and invoke.
#include <functional>
#include <map>
#include <string>
#include <vector>

static int twice(int x) { return 2 * x; }

int main() {
  std::function<int(int)> f = twice;
  std::function<int(int)> g = [k = 3](int x) { return x + k; };
  auto h = std::bind(std::plus<int>(), std::placeholders::_1, 10);
  std::map<std::string, std::function<int(int)>> table{{"f", f}, {"g", g}};
  std::vector<int> r;
  for (auto &e : table) r.push_back(e.second(4));
  return int(std::invoke(h, 5) + r.size()) == 0;
}
