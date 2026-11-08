//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdatomic.h>: <stdatomic.h> in C++ (C++23 compatibility with C11 atomics).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <stdatomic.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::atomic<int> a{5};
  a.fetch_add(3);
  CHECK(a.load() == 8);
  atomic_int b = 2;
  atomic_fetch_add(&b, 5);
  CHECK(atomic_load(&b) == 7);
  atomic_flag f = ATOMIC_FLAG_INIT;
  CHECK(!atomic_flag_test_and_set(&f) && atomic_flag_test_and_set(&f));
  atomic_thread_fence(memory_order_seq_cst);
  printf("stdatomic.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
