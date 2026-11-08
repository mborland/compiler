//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <shared_mutex>: shared_mutex and shared_timed_mutex with shared and exclusive locks.
#include <shared_mutex>
#include <chrono>
#include <mutex>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::shared_mutex m;
  {
    std::shared_lock<std::shared_mutex> a(m), b(m);
    CHECK(a.owns_lock() && b.owns_lock());
    CHECK(!m.try_lock());
  }
  {
    std::unique_lock<std::shared_mutex> w(m);
    CHECK(!m.try_lock_shared());
  }
  CHECK(m.try_lock_shared());
  m.unlock_shared();
  CHECK(m.try_lock());
  m.unlock();
  std::shared_timed_mutex tm;
  CHECK(tm.try_lock_shared_for(std::chrono::milliseconds(1)));
  tm.unlock_shared();
  CHECK(tm.try_lock_for(std::chrono::milliseconds(1)));
  tm.unlock();
  printf("shared_mutex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
