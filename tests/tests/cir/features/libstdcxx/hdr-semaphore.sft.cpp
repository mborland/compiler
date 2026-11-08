//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <semaphore>: counting_semaphore and binary_semaphore.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <semaphore>
#include <chrono>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::counting_semaphore<4> s(2);
  CHECK(s.try_acquire() && s.try_acquire() && !s.try_acquire());
  s.release(2);
  s.acquire();
  CHECK(s.try_acquire() && !s.try_acquire());
  std::binary_semaphore b(0);
  CHECK(!b.try_acquire());
  b.release();
  b.acquire();
  CHECK(!b.try_acquire_for(std::chrono::milliseconds(1)));
  CHECK(std::counting_semaphore<>::max() > 0);
  printf("semaphore: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
