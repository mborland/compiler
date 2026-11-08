//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <unordered_set>: unordered_set and unordered_multiset insertion, lookup and erase.
#include <unordered_set>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::unordered_set<int> s;
  for (int i = 0; i < 500; ++i) s.insert(i % 100);
  CHECK(s.size() == 100);
  CHECK(s.count(99) == 1 && s.count(100) == 0);
  CHECK(!s.insert(5).second && s.insert(1000).second);
  s.erase(5);
  CHECK(s.find(5) == s.end());
  long sum = 0;
  for (int v : s) sum += v;
  CHECK(sum == 4950 - 5 + 1000);
  std::unordered_multiset<int> ms{1, 1, 2};
  CHECK(ms.count(1) == 2 && ms.size() == 3);
  s.reserve(10000);
  CHECK(s.bucket_count() >= 10000);
  printf("unordered_set: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
