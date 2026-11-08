//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <scoped_allocator>: scoped_allocator_adaptor allocation, construction and rebinding.
#include <scoped_allocator>
#include <memory>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::scoped_allocator_adaptor<std::allocator<int>> sa;
  int *p = sa.allocate(3);
  std::allocator_traits<decltype(sa)>::construct(sa, p, 11);
  CHECK(*p == 11);
  std::allocator_traits<decltype(sa)>::destroy(sa, p);
  sa.deallocate(p, 3);
  std::scoped_allocator_adaptor<std::allocator<int>, std::allocator<int>> two;
  CHECK(two.outer_allocator() == std::allocator<int>());
  CHECK(two.inner_allocator().outer_allocator() == std::allocator<int>());
  CHECK(sa == std::scoped_allocator_adaptor<std::allocator<int>>());
  printf("scoped_allocator: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
