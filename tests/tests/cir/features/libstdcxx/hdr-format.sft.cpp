//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <format>: format, format_to, formatted_size, user formatters and compile-time checking.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <format>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
struct P { int x, y; };
template <> struct std::formatter<P> : std::formatter<int> {
  auto format(P p, std::format_context &c) const {
    return std::format_to(c.out(), "({},{})", p.x, p.y);
  }
};
int main() {
  int nine = 9;
  CHECK(std::format("{} {}", 1, "two") == "1 two");
  CHECK(std::format("{:>5}|{:<5}|{:^5}", 1, 2, 3) == "    1|2    |  3  ");
  CHECK(std::format("{:.2f} {:e}", 3.14159, 1500.0) == "3.14 1.500000e+03");
  CHECK(std::format("{:x} {:#X} {:b} {:o}", 255, 255, 5, 8) == "ff 0XFF 101 10");
  CHECK(std::format("{:08.3f}", -3.5) == "-003.500");
  CHECK(std::format("{1} {0}", "a", "b") == "b a");
  CHECK(std::format("{:{}}", 7, 4) == "   7");
  CHECK(std::format("{{}} {}", true) == "{} true");
  CHECK(std::format("{:c} {:s}", 'x', "str") == "x str");
  char buf[32];
  auto r = std::format_to(buf, "{}-{}", 12, 34);
  CHECK(r - buf == 5 && buf[2] == '-');
  CHECK(std::formatted_size("{:5}", 1) == 5);
  CHECK(std::vformat("{}", std::make_format_args(nine)) == "9");
  CHECK(std::format("{}", P{3, 4}) == "(3,4)");
  CHECK(std::format("{:+} {: } {}", 5, 5, -5) == "+5  5 -5");
  CHECK(std::format(L"{}", 5) == L"5");
  CHECK(std::format("{:.3}", "abcdef") == "abc");
  CHECK(std::format("{}", 0.1) == "0.1" && std::format("{}", 100.0) == "100");
  auto tr = std::format_to_n(buf, 4, "{}", 123456);
  CHECK(tr.size == 6 && tr.out - buf == 4);
  printf("format: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
