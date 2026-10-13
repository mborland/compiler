// S5 probe: new (std::nothrow): a null result skips the construction, a
// successful one constructs (also arrays, and a throwing constructor frees the
// storage with operator delete(void *, const std::nothrow_t &)).
#include <cstdio>
#include <cstdlib>
#include <new>
bool fail;
int frees;
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return fail ? nullptr : std::malloc(n); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return fail ? nullptr : std::malloc(n); }
void operator delete(void *p, const std::nothrow_t &) noexcept { ++frees; std::free(p); }
struct A { int v; A() : v(1) { std::puts("A()"); } ~A() { std::puts("~A()"); } };
struct T { T(int x) { std::puts("T()"); if (x) throw x; } };
int main() {
  A *a = new (std::nothrow) A; std::printf("%d\n", a != nullptr); delete a;
  A *b = new (std::nothrow) A[2]; std::printf("%d\n", b != nullptr); delete[] b;
  fail = true;
  A *c = new (std::nothrow) A; std::printf("%d\n", c == nullptr);
  A *d = new (std::nothrow) A[2]; std::printf("%d\n", d == nullptr);
  fail = false;
  try { T *t = new (std::nothrow) T(5); (void)t; } catch (int i) { std::printf("thrown %d frees=%d\n", i, frees); }
  return 0;
}
