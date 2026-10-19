// Real-world TU: smart pointers, optional, variant, string_view.
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

struct Node { int v; std::shared_ptr<Node> next; };

int main() {
  auto n = std::make_shared<Node>(Node{1, nullptr});
  n->next = std::make_shared<Node>(Node{2, nullptr});
  std::unique_ptr<int[]> arr(new int[4]{1, 2, 3, 4});
  std::optional<std::string> o = std::string("x");
  std::variant<int, std::string> var = std::string("abc");
  std::string_view sv = "hello world";
  return int(n->next->v + arr[2] + o->size() + std::get<1>(var).size() +
             sv.find("wor")) == 0;
}
