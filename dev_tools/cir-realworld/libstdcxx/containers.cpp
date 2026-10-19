// Real-world TU: the standard containers.
#include <array>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int main() {
  std::vector<int> v{1, 2, 3};
  std::deque<int> d{4, 5, 6};
  std::list<int> l{7, 8};
  std::forward_list<int> f{9};
  std::map<int, int> m{{1, 2}};
  std::set<int> s{3, 4};
  std::unordered_map<int, int> um{{5, 6}};
  std::unordered_set<int> us{7};
  std::array<int, 3> a{1, 2, 3};
  v.push_back(4); d.push_front(0); l.sort(); f.push_front(1);
  m[3] = 4; s.insert(5); um[7] = 8; us.insert(8);
  return int(v.size() + d.size() + l.size() + m.size() + s.size() +
             um.size() + us.size() + a.size()) == 0;
}
