//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <ctime>: time, gmtime, mktime, strftime, difftime and timespec_get.
#include <ctime>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::time_t t0 = 86400 * 365;
  std::tm *g = std::gmtime(&t0);
  CHECK(g->tm_year == 71 && g->tm_mon == 0 && g->tm_mday == 1 && g->tm_wday == 5);
  char buf[64];
  std::size_t n = std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", g);
  CHECK(n == 19 && buf[0] == '1' && buf[3] == '1' && buf[18] == '0');
  std::tm tm{};
  tm.tm_year = 100; tm.tm_mon = 0; tm.tm_mday = 1; tm.tm_hour = 12;
  tm.tm_isdst = -1;
  CHECK(std::mktime(&tm) != (std::time_t)-1 && tm.tm_yday == 0);
  CHECK(std::difftime(10, 4) == 6.0);
  CHECK(std::time(nullptr) > 1700000000);
  std::timespec ts;
  CHECK(std::timespec_get(&ts, TIME_UTC) == TIME_UTC && ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000);
  CHECK(std::clock() != (std::clock_t)-1);
  char *a = std::asctime(g);
  CHECK(a[0] == 'F');
  printf("ctime: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
