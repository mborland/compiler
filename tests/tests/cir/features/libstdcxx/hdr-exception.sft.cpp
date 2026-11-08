//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <exception>: exception_ptr, nested exceptions, terminate handlers and uncaught_exceptions.
#include <exception>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static void th() { __builtin_abort(); }
int main() {
  std::exception_ptr ep;
  try { throw 42; } catch (...) { ep = std::current_exception(); }
  CHECK(ep != nullptr);
  bool got = false;
  try { std::rethrow_exception(ep); } catch (int v) { got = v == 42; }
  CHECK(got);
  auto mp = std::make_exception_ptr(std::bad_exception());
  try { std::rethrow_exception(mp); } catch (const std::bad_exception &) { got = true; }
  CHECK(got && std::uncaught_exceptions() == 0);
  got = false;
  try {
    try { throw 1; } catch (...) { std::throw_with_nested(std::bad_exception()); }
  } catch (const std::bad_exception &e) {
    try { std::rethrow_if_nested(e); } catch (int n) { got = n == 1; }
  }
  CHECK(got);
  CHECK(std::get_terminate() != nullptr);
  std::terminate_handler old = std::set_terminate(th);
  CHECK(std::get_terminate() == th);
  std::set_terminate(old);
  struct Probe { int *n; ~Probe() { *n += std::uncaught_exceptions(); } };
  int seen = 0;
  try { Probe p{&seen}; throw 3; } catch (int) {}
  CHECK(seen == 1);
  std::exception ex;
  CHECK(ex.what() != nullptr);
  printf("exception: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
