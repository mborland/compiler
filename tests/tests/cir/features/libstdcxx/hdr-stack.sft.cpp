//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stack>: stack adaptor over deque and vector.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <stack>
#include <vector>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::stack<int> s;
  for (int i = 0; i < 20; ++i) s.push(i);
  CHECK(s.size() == 20 && s.top() == 19);
  s.pop();
  CHECK(s.top() == 18);
  std::stack<int, std::vector<int>> v;
  v.push(1); v.push(2);
  CHECK(v.top() == 2 && !v.empty());
  s.emplace(77);
  CHECK(s.top() == 77);
  printf("stack: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
