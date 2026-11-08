//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <deque>: deque push and pop at both ends, indexing, insertion and growth.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <deque>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::deque<int> d;
  for (int i = 0; i < 1000; ++i) { d.push_back(i); d.push_front(-i - 1); }
  CHECK(d.size() == 2000);
  CHECK(d.front() == -1000 && d.back() == 999 && d[1000] == 0);
  d.pop_front(); d.pop_back();
  CHECK(d.size() == 1998 && d.front() == -999);
  d.insert(d.begin() + 5, 12345);
  CHECK(d[5] == 12345);
  d.erase(d.begin() + 5);
  long sum = 0;
  for (int v : d) sum += v;
  CHECK(sum == -999);
  std::deque<int> e(5, 3);
  CHECK(e.size() == 5 && e[4] == 3);
  e.resize(8, 9);
  CHECK(e[7] == 9);
  printf("deque: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
