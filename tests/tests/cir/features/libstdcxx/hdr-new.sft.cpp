//type:rp
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <new>: operator new and delete, placement and nothrow forms, aligned allocation, new_handler.
#include <new>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
static void handler() {}
static int *volatile sink;
int main() {
  int *p = new int(5);
  CHECK(*p == 5);
  delete p;
  int *arr = new int[10];
  arr[9] = 3;
  delete[] arr;
  alignas(int) char buf[sizeof(int)];
  int *q = new (buf) int(7);
  CHECK((void *)q == (void *)buf && *q == 7);
  void *a = ::operator new(64, std::align_val_t(64));
  CHECK(((unsigned long)a & 63) == 0);
  ::operator delete(a, std::align_val_t(64));
  volatile unsigned long huge = 1ul << 40;
  int *n = new (std::nothrow) int[huge];
  sink = n;
  CHECK(sink == nullptr);
  bool caught = false;
  try { int *b = new int[huge]; sink = b; delete[] b; } catch (const std::bad_alloc &) { caught = true; }
  CHECK(caught);
  CHECK(std::get_new_handler() == nullptr);
  std::new_handler old = std::set_new_handler(handler);
  CHECK(old == nullptr && std::get_new_handler() == handler);
  std::set_new_handler(nullptr);
  CHECK(std::launder(q) == q);
  struct alignas(32) Over { char c; };
  Over *o = new Over;
  CHECK(((unsigned long)o & 31) == 0);
  delete o;
  printf("new: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
