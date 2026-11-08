//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <valarray>: valarray arithmetic, reductions, apply, slices and masks.
#include <valarray>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::valarray<double> a(4), b(4);
  for (int i = 0; i < 4; ++i) { a[i] = i + 1; b[i] = 2.0; }
  std::valarray<double> c = a * b + 1.0;
  CHECK(c[0] == 3.0 && c[3] == 9.0);
  CHECK(c.sum() == 24.0 && c.min() == 3.0 && c.max() == 9.0);
  std::valarray<double> d = c.apply([](double x) { return x - 1.0; });
  CHECK(d[1] == 4.0 && d.size() == 4);
  std::valarray<int> v(12);
  for (int i = 0; i < 12; ++i) v[i] = i;
  std::valarray<int> col = v[std::slice(1, 3, 4)];
  CHECK(col.size() == 3 && col[0] == 1 && col[1] == 5 && col[2] == 9);
  std::valarray<bool> m = v > 8;
  CHECK(m[9] && !m[8]);
  std::valarray<int> sh = v.shift(1);
  CHECK(sh[0] == 1 && sh[11] == 0);
  CHECK((v << 1)[3] == 6);
  printf("valarray: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
