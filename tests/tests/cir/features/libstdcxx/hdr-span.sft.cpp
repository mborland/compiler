//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <span>: span views, subspans, byte views and static extents.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <span>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int a[6] = {1, 2, 3, 4, 5, 6};
  std::span<int> s(a);
  CHECK(s.size() == 6 && s.size_bytes() == 6 * sizeof(int));
  CHECK(s.first(2)[1] == 2 && s.last(2)[0] == 5 && s.subspan(1, 3)[2] == 4);
  s[0] = 10;
  CHECK(a[0] == 10);
  int sum = 0;
  for (int v : s) sum += v;
  CHECK(sum == 30);
  std::span<int, 3> f = s.first<3>();
  CHECK(f.extent == 3 && f.size() == 3);
  auto bytes = std::as_bytes(s);
  CHECK(bytes.size() == 24);
  std::span<const int> cs = s;
  CHECK(cs.front() == 10 && cs.back() == 6 && !cs.empty());
  std::span<int> e;
  CHECK(e.empty() && e.data() == nullptr);
  printf("span: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
