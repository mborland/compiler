//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <spanstream>: ispanstream and ospanstream over character buffers (C++23).
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <spanstream>
#include <string>
#include <span>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  char buf[32];
  std::ospanstream os{std::span<char>(buf)};
  os << 123 << ' ' << "abc";
  CHECK(os.span().size() == 7 && buf[0] == '1' && buf[6] == 'c');
  std::ispanstream is{std::span<const char>(buf, 7)};
  int n; std::string w;
  is >> n >> w;
  CHECK(n == 123 && w == "abc");
  char tiny[4];
  std::ospanstream small{std::span<char>(tiny)};
  small << "overflowing";
  CHECK(small.fail() || small.bad());
  std::spanstream io{std::span<char>(buf)};
  io << "77 ";
  io.seekg(0);
  int m;
  io >> m;
  CHECK(m == 77);
  printf("spanstream: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
