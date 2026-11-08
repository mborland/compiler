//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cxxabi.h>: the Itanium C++ ABI runtime interface: demangling and the current exception type.
#include <cxxabi.h>
#include <cstdlib>
#include <typeinfo>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int status = -1;
  char *name = abi::__cxa_demangle("_Z3fooi", nullptr, nullptr, &status);
  CHECK(status == 0 && name != nullptr && name[0] == 'f' && name[3] == '(');
  std::free(name);
  name = abi::__cxa_demangle("_ZNSt6vectorIiSaIiEE9push_backERKi", nullptr, nullptr, &status);
  CHECK(status == 0 && name != nullptr && name[0] == 's' && name[5] == 'v');
  std::free(name);
  name = abi::__cxa_demangle("not a mangled name", nullptr, nullptr, &status);
  CHECK(status == -2 && name == nullptr);
  CHECK(abi::__cxa_current_exception_type() == nullptr);
  try { throw 5; } catch (int) { CHECK(abi::__cxa_current_exception_type() == &typeid(int)); }
  printf("cxxabi.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
