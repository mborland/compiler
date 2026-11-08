//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <memory>: unique_ptr, allocator, construct_at, uninitialized algorithms and std::align.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <memory>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  auto p = std::make_unique<int>(5);
  CHECK(*p == 5);
  auto arr = std::make_unique<int[]>(4);
  arr[3] = 7;
  CHECK(arr[3] == 7);
  struct D { int *n; ~D() { ++*n; } };
  int dtors = 0;
  {
    std::unique_ptr<D> u(new D{&dtors});
    u.reset();
    CHECK(dtors == 1);
    std::unique_ptr<D> u2(new D{&dtors});
  }
  CHECK(dtors == 2);
  std::allocator<int> al;
  int *m = al.allocate(4);
  std::construct_at(m, 3);
  CHECK(*m == 3);
  std::uninitialized_fill_n(m, 4, 8);
  CHECK(m[3] == 8);
  std::destroy_n(m, 4);
  al.deallocate(m, 4);
  alignas(32) char buf[128];
  void *ptr = buf + 1;
  std::size_t space = 127;
  void *r = std::align(32, 16, ptr, space);
  CHECK(r == buf + 32 && space == 96);
  int x = 0;
  CHECK(std::addressof(x) == &x);
  auto q = std::move(p);
  CHECK(!p && *q == 5);
  printf("memory: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
