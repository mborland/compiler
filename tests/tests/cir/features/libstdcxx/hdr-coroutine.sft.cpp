//type:fc
//options:--c++23 --set_flag=coroutines --diag_suppress=cir_warning:--c++23 -O2 --set_flag=coroutines --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <coroutine>: coroutine_handle, noop_coroutine and the trivial awaitables (a coroutine function itself is a CIR limit).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <coroutine>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::coroutine_handle<> h;
  CHECK(!h && h.address() == nullptr);
  auto noop = std::noop_coroutine();
  CHECK(noop.address() != nullptr);
  std::suspend_always sa;
  std::suspend_never sn;
  CHECK(!sa.await_ready() && sn.await_ready());
  CHECK(std::coroutine_handle<>::from_address(nullptr).address() == nullptr);
  CHECK(std::hash<std::coroutine_handle<>>()(h) == std::hash<std::coroutine_handle<>>()(h));
  CHECK(sizeof(std::coroutine_handle<>) == sizeof(void *));
  printf("coroutine: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
