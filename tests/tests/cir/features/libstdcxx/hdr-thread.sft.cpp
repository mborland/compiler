//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <thread>: thread creation and join, this_thread, jthread.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <thread>
#include <chrono>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int x = 0;
  std::thread t([&] { x = 42; });
  CHECK(t.joinable());
  t.join();
  CHECK(x == 42 && !t.joinable());
  std::thread::id main_id = std::this_thread::get_id();
  std::thread::id other;
  std::thread t2([&] { other = std::this_thread::get_id(); });
  t2.join();
  CHECK(other != main_id);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::this_thread::yield();
  CHECK(std::thread::hardware_concurrency() >= 1);
  int y = 0;
  { std::jthread j([&](std::stop_token) { y = 7; }); }
  CHECK(y == 7);
  std::thread e;
  CHECK(!e.joinable() && e.get_id() == std::thread::id());
  printf("thread: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
