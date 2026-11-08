//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <queue>: queue and priority_queue.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <queue>
#include <vector>
#include <functional>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::queue<int> q;
  for (int i = 0; i < 10; ++i) q.push(i);
  CHECK(q.size() == 10 && q.front() == 0 && q.back() == 9);
  q.pop();
  CHECK(q.front() == 1);
  std::priority_queue<int> pq;
  for (int v : {5, 1, 9, 3, 7}) pq.push(v);
  CHECK(pq.top() == 9);
  pq.pop();
  CHECK(pq.top() == 7 && pq.size() == 4);
  std::priority_queue<int, std::vector<int>, std::greater<int>> mn;
  for (int v : {5, 1, 9, 3, 7}) mn.push(v);
  CHECK(mn.top() == 1);
  printf("queue: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
