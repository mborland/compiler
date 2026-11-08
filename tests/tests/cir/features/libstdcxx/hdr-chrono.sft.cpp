//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <chrono>: durations, time points, clocks, calendar dates and hh_mm_ss.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <chrono>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  using namespace std::chrono;
  using namespace std::chrono_literals;
  auto d = 1s + 500ms;
  CHECK(d.count() == 1500 && duration_cast<seconds>(d).count() == 1);
  CHECK(duration_cast<milliseconds>(2h).count() == 7200000);
  CHECK((3min <=> 180s) == 0 && 1h > 59min);
  auto t0 = steady_clock::now();
  auto t1 = steady_clock::now();
  CHECK(t1 >= t0);
  CHECK(system_clock::now().time_since_epoch().count() > 0);
  year_month_day ymd{2024y / February / 29};
  CHECK(ymd.ok() && sys_days(ymd).time_since_epoch().count() == 19782);
  CHECK(weekday(sys_days(ymd)) == Thursday);
  CHECK(!year_month_day{2023y / February / 29}.ok());
  CHECK(year(2024).is_leap());
  hh_mm_ss<seconds> hms(3725s);
  CHECK(hms.hours().count() == 1 && hms.minutes().count() == 2 && hms.seconds().count() == 5);
  sys_days a = 2000y / January / 1, b = 2000y / March / 1;
  CHECK((b - a).count() == 60);
  auto tp = time_point_cast<seconds>(sys_days(1970y / January / 2));
  CHECK(tp.time_since_epoch().count() == 86400);
  CHECK(floor<minutes>(90s).count() == 1 && round<minutes>(90s).count() == 2);
  CHECK(std::chrono::duration<double>(1.5s).count() == 1.5);
  printf("chrono: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
