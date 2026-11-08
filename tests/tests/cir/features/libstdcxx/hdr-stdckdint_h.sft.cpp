//type:rp
//options:--c++26 --diag_suppress=cir_warning:--c++26 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdckdint.h>: <stdckdint.h> checked integer arithmetic (C++26).
#include <stdckdint.h>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  int r = 0;
  CHECK(!ckd_add(&r, 2, 3) && r == 5);
  CHECK(ckd_add(&r, 2147483647, 1) && r == -2147483647 - 1);
  CHECK(!ckd_sub(&r, 5, 7) && r == -2);
  CHECK(!ckd_mul(&r, 1000, 1000) && r == 1000000);
  CHECK(ckd_mul(&r, 100000, 100000));
  unsigned char u = 0;
  CHECK(ckd_add(&u, 200, 100) && u == 44);
  long long ll = 0;
  CHECK(!ckd_mul(&ll, 3000000000LL, 3) && ll == 9000000000LL);
  printf("stdckdint.h: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
