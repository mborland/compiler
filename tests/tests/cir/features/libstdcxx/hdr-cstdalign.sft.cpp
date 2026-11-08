//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdalign>: the deprecated <cstdalign>.
#include <cstdalign>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
#ifdef __alignas_is_defined
  CHECK(__alignas_is_defined == 1);
#endif
  alignas(16) char buf[16];
  CHECK(((unsigned long)buf & 15) == 0);
  CHECK(alignof(double) == 8);
  printf("cstdalign: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
