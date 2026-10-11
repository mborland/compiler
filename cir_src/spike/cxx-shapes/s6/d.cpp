#include <cstdio>
#include <cstdlib>
#include <new>
struct D { int x; D() : x(1) { std::printf("D() %p\n", (void*)this); } ~D() { std::printf("~D() %p\n", (void*)this); } };
struct E { int y; ~E() { std::printf("~E()\n"); }
  static void operator delete[](void *p, std::size_t sz) { std::printf("E::delete[] sz=%zu\n", sz); ::operator delete[](p); } };
D *mk(unsigned n) { return new D[n]; }
void rm(D *p) { delete[] p; }
void rm_e(E *p) { delete[] p; }
void rm_i(int *p) { delete[] p; }
void rm_one(D *p) { delete p; }
int main() {
  D *a = mk(3); std::printf("a=%p cookie=%zu\n", (void*)a, ((std::size_t*)a)[-1]);
  rm(a);
  E *e = new E[2]; std::printf("e cookie=%zu\n", ((std::size_t*)e)[-1]); rm_e(e);
  int *i = new int[4]; rm_i(i);
  D *o = new D; rm_one(o);
  return 0;
}
