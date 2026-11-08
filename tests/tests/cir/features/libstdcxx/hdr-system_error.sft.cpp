//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <system_error>: error_code, error_condition, errc, categories and system_error.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <system_error>
#include <cerrno>
#include <string>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::error_code ec = std::make_error_code(std::errc::invalid_argument);
  CHECK(ec == std::errc::invalid_argument && ec.value() == EINVAL);
  CHECK(ec.category() == std::generic_category() && !!ec);
  CHECK(ec.message().size() > 0);
  std::error_code none;
  CHECK(!none && none.category() == std::system_category());
  std::error_condition cond = std::errc::no_such_file_or_directory;
  CHECK(std::error_code(ENOENT, std::generic_category()) == cond);
  bool caught = false;
  try { throw std::system_error(ec, "ctx"); } catch (const std::system_error &e) { caught = e.code() == ec && e.what()[0] == 'c'; }
  CHECK(caught);
  CHECK(!std::is_error_code_enum_v<std::errc> && std::is_error_condition_enum_v<std::errc>);
  CHECK(std::string(std::generic_category().name()) == "generic");
  CHECK(std::hash<std::error_code>()(ec) == std::hash<std::error_code>()(ec));
  printf("system_error: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
