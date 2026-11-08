//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <typeindex>: type_index as a map key and in comparisons.
#include <typeindex>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::type_index a(typeid(int)), b(typeid(double)), c(typeid(int));
  CHECK(a == c && a != b);
  CHECK((a < b) != (b < a));
  CHECK(a.hash_code() == c.hash_code());
  CHECK(std::hash<std::type_index>()(a) == a.hash_code());
  struct Base { virtual ~Base() {} };
  struct Der : Base {};
  Der d;
  Base &br = d;
  CHECK(std::type_index(typeid(br)) == std::type_index(typeid(Der)));
  CHECK(a.name() != nullptr && a.name()[0] != 0);
  std::type_index arr[3] = {a, b, std::type_index(typeid(char))};
  int n = 0;
  for (auto &t : arr) n += t == a;
  CHECK(n == 1);
  printf("typeindex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
