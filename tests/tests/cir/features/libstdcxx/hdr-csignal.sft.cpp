//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <csignal>: signal, raise and a handler that sets a sig_atomic_t.
#include <csignal>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static volatile std::sig_atomic_t got;
static void handler(int sig) { if (sig == SIGUSR1) got = 1; }
int main() {
  CHECK(std::signal(SIGUSR1, handler) != SIG_ERR);
  CHECK(std::raise(SIGUSR1) == 0);
  CHECK(got == 1);
  CHECK(std::signal(SIGUSR1, SIG_DFL) == handler);
  CHECK(SIGINT == 2 && SIGTERM == 15 && SIGABRT == 6 && SIGSEGV == 11 && SIGFPE == 8);
  printf("csignal: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
