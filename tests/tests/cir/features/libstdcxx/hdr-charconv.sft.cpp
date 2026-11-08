//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <charconv>: to_chars and from_chars for integers and floating point.
#include <charconv>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, 12345);
  CHECK(r.ec == std::errc() && r.ptr - buf == 5 && buf[0] == '1' && buf[4] == '5');
  r = std::to_chars(buf, buf + sizeof buf, -255, 16);
  CHECK(r.ptr - buf == 3 && buf[0] == '-' && buf[1] == 'f' && buf[2] == 'f');
  int v = 0;
  auto f = std::from_chars("9876xyz", "9876xyz" + 7, v);
  CHECK(f.ec == std::errc() && v == 9876 && *f.ptr == 'x');
  f = std::from_chars("zz", "zz" + 2, v);
  CHECK(f.ec == std::errc::invalid_argument);
  unsigned char small = 0;
  f = std::from_chars("300", "300" + 3, small);
  CHECK(f.ec == std::errc::result_out_of_range);
  r = std::to_chars(buf, buf + sizeof buf, 0.5);
  CHECK(r.ptr - buf == 3 && buf[0] == '0' && buf[1] == '.' && buf[2] == '5');
  double d = 0;
  f = std::from_chars("2.75e1", "2.75e1" + 6, d);
  CHECK(f.ec == std::errc() && d == 27.5);
  r = std::to_chars(buf, buf + 2, 123456);
  CHECK(r.ec == std::errc::value_too_large);
  r = std::to_chars(buf, buf + sizeof buf, 255u, 2);
  CHECK(r.ptr - buf == 8);
  printf("charconv: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
