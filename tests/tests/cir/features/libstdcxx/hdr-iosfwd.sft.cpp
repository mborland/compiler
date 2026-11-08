//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <iosfwd>: forward declarations of the stream classes and the stream position types.
#include <iosfwd>
#include <type_traits>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static int touch(std::ostream *o) { return o ? 1 : 0; }
int main() {
  static_assert(std::is_same_v<std::ostream, std::basic_ostream<char>>);
  static_assert(std::is_same_v<std::istream, std::basic_istream<char, std::char_traits<char>>>);
  static_assert(sizeof(std::streamoff) == 8 && sizeof(std::streamsize) == 8);
  std::ostream *po = nullptr;
  std::istream *pi = nullptr;
  std::streambuf *pb = nullptr;
  std::string *ps = nullptr;
  CHECK(po == nullptr && pi == nullptr && pb == nullptr && ps == nullptr);
  std::streampos pos(10);
  CHECK(std::streamoff(pos) == 10);
  CHECK(touch(po) == 0);
  std::fpos<std::mbstate_t> fp2(5);
  CHECK(std::streamoff(fp2 + 3) == 8);
  printf("iosfwd: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
