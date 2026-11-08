//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <random>: engines with their reference outputs, distributions, shuffle and seed_seq.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <random>
#include <algorithm>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::mt19937 g(42);
  CHECK(g() == 1608637542u);
  std::mt19937 g2;
  for (int i = 1; i < 10000; ++i) g2();
  CHECK(g2() == 4123659995u);
  std::mt19937_64 g3;
  for (int i = 1; i < 10000; ++i) g3();
  CHECK(g3() == 9981545732273789042ull);
  std::minstd_rand0 lc;
  CHECK(lc() == 16807u);
  std::uniform_int_distribution<int> u(10, 20);
  std::mt19937 r(7);
  for (int i = 0; i < 1000; ++i) { int x = u(r); CHECK(x >= 10 && x <= 20); }
  std::uniform_real_distribution<double> ur(0.0, 1.0);
  for (int i = 0; i < 1000; ++i) { double x = ur(r); CHECK(x >= 0.0 && x < 1.0); }
  std::normal_distribution<double> nd(0.0, 1.0);
  double s = 0;
  for (int i = 0; i < 1000; ++i) s += nd(r);
  CHECK(s > -200 && s < 200);
  std::bernoulli_distribution bd(1.0);
  CHECK(bd(r));
  std::discrete_distribution<int> dd({0.0, 1.0, 0.0});
  CHECK(dd(r) == 1);
  int a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  std::shuffle(a, a + 8, r);
  int t = 0;
  for (int x : a) t += x;
  CHECK(t == 36);
  std::seed_seq sq{1, 2, 3};
  std::mt19937 gs(sq);
  CHECK(gs() != 0 || gs() != 0);
  std::random_device rd;
  (void)rd();
  printf("random: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
