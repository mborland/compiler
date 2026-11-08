//type:rp
//options:--c++23:--c++23 -O2
//use_system_includes: true
//require:BACK_END_TYPE 3
/* cxx23.extended_floating_types: extended floating-point types: _Float32, _Float64 and _Float16 arithmetic on run-time values. */
extern "C" int printf(const char *, ...);
extern "C" int puts(const char *);
static int failures;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #__VA_ARGS__); } } while (0)
#include <stdfloat>

static volatile int vi = 3;

static _Float16 add16(_Float16 a, _Float16 b) { return a + b; }
static _Float16 mul16(_Float16 a, _Float16 b) { return a * b; }
static float widen16(_Float16 a) { return (float)a; }
static _Float32 add32(_Float32 a, _Float32 b) { return a + b; }
static _Float64 mul64(_Float64 a, _Float64 b) { return a * b; }
static std::float16_t sub16(std::float16_t a, std::float16_t b) { return a - b; }

static void run()
{
  int n = vi;
  _Float16 x = (_Float16)n;
  _Float16 y = add16(x, x);
  CHECK(widen16(y) == 6.0f && widen16(x) == 3.0f);
  CHECK(widen16(mul16(x, y)) == 18.0f);
  CHECK(widen16(sub16((std::float16_t)n, (std::float16_t)(n - 2))) == 2.0f);
  _Float16 arr[4];
  for (int i = 0; i < 4; ++i) arr[i] = (_Float16)(i + n);
  float sum = 0;
  for (int i = 0; i < 4; ++i) sum += widen16(arr[i]);
  CHECK(sum == 3 + 4 + 5 + 6);
  CHECK(sizeof(_Float16) == 2 && sizeof(_Float32) == 4 && sizeof(_Float64) == 8);
  _Float32 a = (_Float32)n;
  CHECK(add32(a, a) == 6.0f);
  _Float64 b = (_Float64)n;
  CHECK(mul64(b, b) == 9.0);
  CHECK(widen16(x > (_Float16)(n - 1) ? x : (_Float16)(n - 3)) == 3.0f);
}

int main()
{
  run();
  if (failures == 0) puts("cxx23.extended_floating_types ok");
  return failures != 0;
}
