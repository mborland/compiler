//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ratio>: ratio arithmetic and comparison at compile time.
#include <ratio>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  using third = std::ratio<1, 3>;
  using sixth = std::ratio<1, 6>;
  using sum = std::ratio_add<third, sixth>;
  static_assert(sum::num == 1 && sum::den == 2);
  static_assert(std::ratio_equal_v<sum, std::ratio<2, 4>>);
  static_assert(std::ratio_less_v<sixth, third> && !std::ratio_greater_v<sixth, third>);
  static_assert(std::ratio_multiply<third, std::ratio<3, 1>>::num == 1);
  static_assert(std::ratio_divide<third, sixth>::num == 2 && std::ratio_divide<third, sixth>::den == 1);
  static_assert(std::ratio_subtract<third, sixth>::den == 6);
  static_assert(std::kilo::num == 1000 && std::milli::den == 1000 && std::mega::num == 1000000);
  static_assert(std::ratio<-4, 6>::num == -2 && std::ratio<-4, 6>::den == 3);
  CHECK(sum::num * 2 == sum::den);
  CHECK(std::giga::num == 1000000000L);
  printf("ratio: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
