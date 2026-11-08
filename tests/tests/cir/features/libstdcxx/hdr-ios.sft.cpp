//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ios>: ios_base flags, basic_ios state bits, exceptions masks and error codes.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <ios>
#include <system_error>
#include <streambuf>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct NullBuf : std::streambuf {};
int main() {
  std::ios nb(nullptr);
  CHECK(nb.rdstate() == std::ios::badbit && !nb.good() && nb.bad() && !nb);
  NullBuf sbuf;
  std::ios s(&sbuf);
  CHECK(s.good());
  CHECK(s.good() && static_cast<bool>(s));
  s.setstate(std::ios::eofbit);
  CHECK(s.eof() && !s.fail());
  s.clear();
  std::ios_base::fmtflags f = s.flags();
  s.setf(std::ios::hex, std::ios::basefield);
  CHECK((s.flags() & std::ios::basefield) == std::ios::hex);
  s.flags(f);
  CHECK(s.precision() == 6 && s.width() == 0 && s.fill() == ' ');
  s.exceptions(std::ios::failbit);
  bool caught = false;
  try { s.setstate(std::ios::failbit); } catch (const std::ios_base::failure &) { caught = true; }
  CHECK(caught);
  CHECK(std::make_error_code(std::io_errc::stream) != std::error_code());
  CHECK(std::iostream_category().name()[0] == 'i');
  s.exceptions(std::ios::goodbit);
  CHECK(std::ios_base::openmode(std::ios::in | std::ios::out) != std::ios::in);
  CHECK(std::ios::beg != std::ios::end);
  std::ios_base &b = std::hex(s);
  CHECK((b.flags() & std::ios::hex) != 0);
  printf("ios: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
