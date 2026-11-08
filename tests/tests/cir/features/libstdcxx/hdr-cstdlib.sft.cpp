//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdlib>: abs, div, strtol, strtod, qsort, bsearch, malloc and getenv.
#include <cstdlib>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  CHECK(std::abs(-3) == 3 && std::labs(-4L) == 4 && std::llabs(-5LL) == 5);
  std::div_t d = std::div(17, 5);
  CHECK(d.quot == 3 && d.rem == 2);
  std::ldiv_t ld = std::ldiv(-17L, 5L);
  CHECK(ld.quot == -3 && ld.rem == -2);
  char *end;
  CHECK(std::strtol("  -123abc", &end, 10) == -123 && *end == 'a');
  CHECK(std::strtol("ff", nullptr, 16) == 255 && std::atoi("42") == 42);
  CHECK(std::strtod("2.5e2x", &end) == 250.0 && *end == 'x');
  CHECK(std::strtoul("4294967295", nullptr, 10) == 4294967295UL);
  int a[7] = {5, 3, 9, 1, 7, 2, 8};
  std::qsort(a, 7, sizeof(int), [](const void *x, const void *y) { return *(const int *)x - *(const int *)y; });
  CHECK(a[0] == 1 && a[6] == 9);
  int key = 7;
  int *f = (int *)std::bsearch(&key, a, 7, sizeof(int), [](const void *x, const void *y) { return *(const int *)x - *(const int *)y; });
  CHECK(f == a + 4);
  int *m = (int *)std::malloc(10 * sizeof(int));
  m[9] = 1;
  m = (int *)std::realloc(m, 20 * sizeof(int));
  CHECK(m[9] == 1);
  std::free(m);
  int *z = (int *)std::calloc(4, sizeof(int));
  CHECK(z[3] == 0);
  std::free(z);
  CHECK(std::getenv("EDG_CIR_NO_SUCH_VARIABLE") == nullptr);
  void *al = std::aligned_alloc(64, 128);
  CHECK(((unsigned long)al & 63) == 0);
  std::free(al);
  printf("cstdlib: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
