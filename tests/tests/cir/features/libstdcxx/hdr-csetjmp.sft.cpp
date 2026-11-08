//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <csetjmp>: setjmp and longjmp with the value kept in a volatile object.
#include <csetjmp>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static void deeper(std::jmp_buf jb, int v) { std::longjmp(jb, v); }
int main() {
  std::jmp_buf jb;
  volatile int count = 0;
  int r = setjmp(jb);
  if (r == 0) {
    count = count + 1;
    deeper(jb, 7);
    CHECK(false);
  } else {
    CHECK(r == 7);
  }
  CHECK(count == 1);
  printf("csetjmp: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
