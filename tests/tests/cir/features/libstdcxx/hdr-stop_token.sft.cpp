//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stop_token>: stop_source, stop_token and stop_callback.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <stop_token>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::stop_source src;
  std::stop_token tok = src.get_token();
  CHECK(tok.stop_possible() && !tok.stop_requested());
  int called = 0;
  {
    std::stop_callback cb(tok, [&] { ++called; });
    CHECK(src.request_stop() && called == 1);
    CHECK(!src.request_stop());
  }
  CHECK(tok.stop_requested());
  std::stop_callback late(tok, [&] { ++called; });
  CHECK(called == 2);
  std::stop_token none;
  CHECK(!none.stop_possible());
  std::stop_source ns(std::nostopstate);
  CHECK(!ns.stop_possible());
  printf("stop_token: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
