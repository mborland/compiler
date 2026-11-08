//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <mutex>: mutex, lock_guard, unique_lock, scoped_lock, recursive and timed mutexes, call_once.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <mutex>
#include <chrono>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::mutex m;
  {
    std::lock_guard<std::mutex> g(m);
    CHECK(!m.try_lock());
  }
  CHECK(m.try_lock());
  m.unlock();
  std::unique_lock<std::mutex> u(m);
  CHECK(u.owns_lock());
  u.unlock();
  CHECK(!u.owns_lock());
  std::unique_lock<std::mutex> dl(m, std::defer_lock);
  CHECK(dl.try_lock() && dl.owns_lock());
  dl.unlock();
  std::mutex m2;
  {
    std::scoped_lock both(m, m2);
    CHECK(!m.try_lock() && !m2.try_lock());
  }
  std::recursive_mutex rm;
  rm.lock(); rm.lock();
  rm.unlock(); rm.unlock();
  std::timed_mutex tm;
  CHECK(tm.try_lock_for(std::chrono::milliseconds(1)));
  tm.unlock();
  std::once_flag once;
  int n = 0;
  std::call_once(once, [&] { ++n; });
  std::call_once(once, [&] { ++n; });
  CHECK(n == 1);
  std::lock(m, m2);
  m.unlock(); m2.unlock();
  CHECK(std::try_lock(m, m2) == -1);
  m.unlock(); m2.unlock();
  printf("mutex: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
