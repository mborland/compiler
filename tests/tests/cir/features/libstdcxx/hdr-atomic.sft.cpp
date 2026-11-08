//type:fn
//options:--c++23 --diag_suppress=cir_warning:--c++23 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <atomic>: atomic integers, pointers, flags, compare_exchange, fences and atomic_ref.
// Negative test: the CIR back end diagnoses this and drops the definition; it must fail at -O0 and -O2.
#include <atomic>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  std::atomic<int> a{5};
  CHECK(a.load() == 5);
  a.store(7);
  CHECK(a.exchange(9) == 7 && a == 9);
  CHECK(a.fetch_add(3) == 9 && a.fetch_sub(2) == 12 && a.load() == 10);
  CHECK((a |= 5) == 15 && (a &= 6) == 6 && (a ^= 2) == 4);
  int expected = 4;
  CHECK(a.compare_exchange_strong(expected, 20) && a == 20);
  expected = 1;
  CHECK(!a.compare_exchange_strong(expected, 30) && expected == 20);
  std::atomic<long> l{1};
  CHECK(++l == 2 && l++ == 2 && l == 3);
  int arr[4] = {};
  std::atomic<int *> p{arr};
  p.fetch_add(2);
  CHECK(p.load() == arr + 2);
  std::atomic_flag f = ATOMIC_FLAG_INIT;
  CHECK(!f.test_and_set() && f.test_and_set());
  f.clear();
  CHECK(!f.test());
  std::atomic<bool> b{false};
  b = true;
  CHECK(b.load(std::memory_order_acquire));
  std::atomic_thread_fence(std::memory_order_seq_cst);
  std::atomic_signal_fence(std::memory_order_acq_rel);
  int plain = 3;
  std::atomic_ref<int> ref(plain);
  ref.fetch_add(4);
  CHECK(plain == 7);
  std::atomic<double> d{1.5};
  d.fetch_add(1.0);
  CHECK(d.load() == 2.5);
  struct Pair { int a, b; };
  std::atomic<Pair> ap{Pair{1, 2}};
  CHECK(ap.load().b == 2);
  CHECK(std::atomic<int>::is_always_lock_free);
  printf("atomic: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
