//type:rp
//options:--c++20 --diag_suppress=cir_warning:--c++20 -O2 --diag_suppress=cir_warning
//use_system_includes: true
//require:BACK_END_TYPE 3
// <memory_resource>: monotonic_buffer_resource, polymorphic_allocator and the default resource.
#include <memory_resource>
#include <new>
#include <cstddef>
extern "C" int printf(const char *, ...);
static int fails;
#define CHECK(...) \
  do { if (!(__VA_ARGS__)) { printf("check failed at line %d\n", __LINE__); ++fails; } } while (0)
int main() {
  alignas(16) char buf[1024];
  std::pmr::monotonic_buffer_resource mr(buf, sizeof buf, std::pmr::null_memory_resource());
  void *a = mr.allocate(100, 16);
  void *b = mr.allocate(100, 16);
  CHECK(a != b);
  CHECK((char *)a >= buf && (char *)a < buf + sizeof buf);
  CHECK(((unsigned long)a & 15) == 0);
  std::pmr::polymorphic_allocator<int> pa(&mr);
  int *p = pa.allocate(10);
  p[9] = 4;
  CHECK(p[9] == 4);
  pa.deallocate(p, 10);
  auto *nd = std::pmr::new_delete_resource();
  void *h = nd->allocate(64, 8);
  CHECK(h != nullptr);
  nd->deallocate(h, 64, 8);
  CHECK(nd->is_equal(*std::pmr::new_delete_resource()));
  bool caught = false;
  try { (void)std::pmr::null_memory_resource()->allocate(1, 1); } catch (const std::bad_alloc &) { caught = true; }
  CHECK(caught);
  std::pmr::unsynchronized_pool_resource pool;
  void *q = pool.allocate(40, 8);
  CHECK(q != nullptr);
  pool.deallocate(q, 40, 8);
  CHECK(std::pmr::get_default_resource() != nullptr);
  printf("memory_resource: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
