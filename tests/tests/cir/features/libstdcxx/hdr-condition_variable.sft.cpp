//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <condition_variable>: condition_variable timed waits with predicates and notification without waiters.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <condition_variable>
#include <mutex>
#include <chrono>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::mutex m;
  std::condition_variable cv;
  std::unique_lock<std::mutex> lk(m);
  bool ready = false;
  CHECK(!cv.wait_for(lk, std::chrono::milliseconds(2), [&] { return ready; }));
  ready = true;
  CHECK(cv.wait_for(lk, std::chrono::milliseconds(2), [&] { return ready; }));
  cv.wait(lk, [&] { return ready; });
  CHECK(cv.wait_until(lk, std::chrono::steady_clock::now() + std::chrono::milliseconds(1)) == std::cv_status::timeout);
  cv.notify_one();
  cv.notify_all();
  std::condition_variable_any cva;
  CHECK(!cva.wait_for(lk, std::chrono::milliseconds(1), [] { return false; }));
  CHECK(lk.owns_lock());
  printf("condition_variable: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
