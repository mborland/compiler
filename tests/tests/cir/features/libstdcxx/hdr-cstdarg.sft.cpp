//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cstdarg>: va_list, va_start, va_arg, va_copy and vsnprintf.
#include <cstdarg>
#include <cstdio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static int sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int s = 0;
  for (int i = 0; i < n; ++i) s += va_arg(ap, int);
  va_end(ap);
  return s;
}
static double dsum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  double s = 0;
  for (int i = 0; i < n; ++i) s += va_arg(ap, double);
  va_end(ap);
  return s;
}
static int fmt(char *b, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int n = std::vsnprintf(b, 32, f, ap);
  va_end(ap);
  return n;
}
static int twice(int n, ...) {
  va_list ap, ap2;
  va_start(ap, n);
  va_copy(ap2, ap);
  int s = 0;
  for (int i = 0; i < n; ++i) s += va_arg(ap, int);
  for (int i = 0; i < n; ++i) s += va_arg(ap2, int);
  va_end(ap2);
  va_end(ap);
  return s;
}
int main() {
  CHECK(sum(4, 1, 2, 3, 4) == 10);
  CHECK(sum(0) == 0);
  CHECK(dsum(3, 1.5, 2.5, 3.0) == 7.0);
  char buf[32];
  CHECK(fmt(buf, "%d:%s", 5, "x") == 3 && buf[0] == '5' && buf[2] == 'x');
  CHECK(twice(3, 1, 2, 3) == 12);
  printf("cstdarg: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
