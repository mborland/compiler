//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <version>: the <version> feature-test macros.
#include <version>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(__cplusplus >= 202002L);
  CHECK(__cpp_lib_optional >= 201606L && __cpp_lib_variant >= 201606L);
  CHECK(__cpp_lib_ranges >= 201911L && __cpp_lib_span >= 202002L);
  CHECK(__cpp_lib_string_view >= 201606L && __cpp_lib_bit_cast >= 201806L);
  CHECK(__cpp_lib_concepts >= 202002L && __cpp_lib_three_way_comparison >= 201907L);
#if __cplusplus > 202002L
  CHECK(__cpp_lib_expected >= 202211L && __cpp_lib_print >= 202207L);
  CHECK(__cpp_lib_flat_map >= 202207L);
#endif
  printf("version: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
