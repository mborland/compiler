//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <future>: promise, future, packaged_task and async.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <future>
#include <stdexcept>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  p.set_value(5);
  CHECK(f.get() == 5);
  std::packaged_task<int(int)> pt([](int x) { return x * 2; });
  auto pf = pt.get_future();
  pt(21);
  CHECK(pf.get() == 42);
  auto a = std::async(std::launch::async, [] { return 7; });
  CHECK(a.get() == 7);
  auto d = std::async(std::launch::deferred, [] { return 9; });
  CHECK(d.get() == 9);
  std::promise<int> ep;
  auto ef = ep.get_future();
  ep.set_exception(std::make_exception_ptr(std::runtime_error("boom")));
  bool caught = false;
  try { ef.get(); } catch (const std::runtime_error &) { caught = true; }
  CHECK(caught);
  std::shared_future<int> sf = std::async(std::launch::deferred, [] { return 3; }).share();
  CHECK(sf.get() == 3 && sf.get() == 3);
  printf("future: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
