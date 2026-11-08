//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <cerrno>: errno values set by library calls.
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  errno = 0;
  CHECK(errno == 0);
  char *end;
  (void)std::strtol("99999999999999999999999", &end, 10);
  CHECK(errno == ERANGE);
  errno = 0;
  std::FILE *f = std::fopen("/edg-cir-nonexistent/dir/file", "r");
  CHECK(f == nullptr && errno == ENOENT);
  errno = EINVAL;
  CHECK(errno == EINVAL && EDOM != ERANGE);
  errno = 0;
  CHECK(std::strerror(ENOENT) != nullptr);
  printf("cerrno: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
