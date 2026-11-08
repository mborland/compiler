//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <unordered_map>: unordered_map and unordered_multimap insertion, lookup, rehash and erase.
#include <unordered_map>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::unordered_map<int, int> m;
  for (int i = 0; i < 1000; ++i) m[i * 3] = i;
  CHECK(m.size() == 1000);
  CHECK(m.at(300) == 100 && m.find(301) == m.end());
  long sum = 0;
  for (auto &kv : m) sum += kv.second;
  CHECK(sum == 499500);
  m.erase(300);
  CHECK(m.count(300) == 0 && m.size() == 999);
  m.rehash(4096);
  CHECK(m.bucket_count() >= 4096 && m.at(600) == 200);
  CHECK(m.load_factor() < 1.0f);
  struct H { std::size_t operator()(int x) const { return std::size_t(x) * 31; } };
  std::unordered_map<int, char, H> h{{1, 'a'}, {2, 'b'}};
  CHECK(h[2] == 'b');
  std::unordered_multimap<int, int> mm{{1, 1}, {1, 2}, {2, 3}};
  CHECK(mm.count(1) == 2);
  printf("unordered_map: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
