//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdexcept>: the standard exception classes, their hierarchy and what().
#include <stdexcept>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
template <class E> bool check(const char *m) {
  try { throw E(m); } catch (const E &e) { return e.what()[0] == m[0]; }
  return false;
}
int main() {
  CHECK(check<std::logic_error>("l") && check<std::runtime_error>("r"));
  CHECK(check<std::domain_error>("d") && check<std::invalid_argument>("i"));
  CHECK(check<std::length_error>("le") && check<std::out_of_range>("o"));
  CHECK(check<std::range_error>("re") && check<std::overflow_error>("ov"));
  CHECK(check<std::underflow_error>("un"));
  bool caught = false;
  try { throw std::out_of_range("oor"); } catch (const std::logic_error &e) { caught = e.what()[0] == 'o'; }
  CHECK(caught);
  caught = false;
  try { throw std::overflow_error(std::string("ovf")); } catch (const std::runtime_error &e) { caught = e.what()[2] == 'f'; }
  CHECK(caught);
  caught = false;
  try { throw std::invalid_argument("x"); } catch (const std::exception &e) { caught = e.what()[0] == 'x'; }
  CHECK(caught);
  printf("stdexcept: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
