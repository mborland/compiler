//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <stdfloat>: the extended floating-point types of <stdfloat> (C++23): float32_t and float64_t.
#include <stdfloat>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
#ifdef __STDCPP_FLOAT32_T__
  std::float32_t a = 1.5f32;
  CHECK(a + a == 3.0f32 && sizeof(a) == 4);
  std::float32_t m = a * 2.0f32 - 1.0f32;
  CHECK(m == 2.0f32);
#endif
#ifdef __STDCPP_FLOAT64_T__
  std::float64_t b = 2.5f64;
  CHECK(b * b == 6.25f64 && sizeof(b) == 8);
#endif
  printf("float32 %d float64 %d\n",
#ifdef __STDCPP_FLOAT32_T__
         1,
#else
         0,
#endif
#ifdef __STDCPP_FLOAT64_T__
         1
#else
         0
#endif
  );
  printf("stdfloat: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
