//options: --c++20
// M4a probe: assoc_operator_delete_routine of virtual destructors (D0):
// class-scope operator delete, sized, aligned, destroying, global fallback.
#include <cstdio>
#include <cstddef>
#include <new>
struct G { virtual ~G() { std::puts("~G"); } };                 // ::operator delete
struct S { virtual ~S() {} static void operator delete(void *p, std::size_t) { std::puts("S sized"); ::operator delete(p); } };
struct S2 { virtual ~S2() {} static void operator delete(void *p) { std::puts("S2 plain"); ::operator delete(p); }
            static void operator delete(void *p, std::size_t) { std::puts("S2 sized"); ::operator delete(p); } };
struct alignas(32) AL { virtual ~AL() {} };                      // aligned global delete
struct AL2 { virtual ~AL2() {} static void operator delete(void *p, std::align_val_t) { std::puts("AL2 aligned"); ::operator delete(p, std::align_val_t(8)); } };
struct DD { virtual ~DD() {} void operator delete(DD *p, std::destroying_delete_t) { std::puts("DD destroying"); p->~DD(); ::operator delete(p); } };
struct Dd : DD { };
struct NP { virtual ~NP() {} void *operator new(std::size_t n) { return ::operator new(n); } void operator delete(void *p) { std::puts("NP delete"); ::operator delete(p); } };
int main() {
  G *g = new G; delete g;
  S *s = new S; delete s;
  S2 *s2 = new S2; delete s2;
  AL *a = new AL; delete a;
  AL2 *a2 = new AL2; delete a2;
  DD *d = new Dd; delete d;
  NP *n = new NP; delete n;
  return 0;
}
