// Host of cxx-s4-destroying-delete-virtual.test: the destroying operator
// delete of A, called by the CIR deleting destructor D0 (which runs no D1).
// The operator destroys the object with a virtual call and frees it; the
// program fails at exit unless it ran exactly once.
#include <cstddef>
#include <new>
#include <unistd.h>

struct A {
  virtual int f(int);
  virtual ~A();
  int x;
  A();
  static void operator delete(A *, std::destroying_delete_t, std::size_t);
};

static int g_dest;

void A::operator delete(A *p, std::destroying_delete_t, std::size_t n) {
  ++g_dest;
  if (n != 16)
    _exit(4);
  p->~A();
  ::operator delete(p, n);
}

__attribute__((destructor)) static void check() {
  if (g_dest != 1)
    _exit(3);
}
