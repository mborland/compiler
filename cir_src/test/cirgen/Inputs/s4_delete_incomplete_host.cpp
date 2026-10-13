// Host of cxx-s4-delete-incomplete.test: the CIR unit deletes a pointer to a class
// that is incomplete there (C++23): only operator delete(void *) is called, with a
// null check.
#include <cstdlib>
struct Z { int x; };
Z *p, *q;
void *v;
static int calls, acalls;
void operator delete(void *q) noexcept { ++calls; std::free(q); }
void operator delete[](void *r) noexcept { ++acalls; std::free(r); }
extern "C" void c_del();
extern "C" void c_dela();
extern "C" void c_delv();
int main() {
  p = nullptr;
  c_del();
  if (calls != 0) return 1;
  p = static_cast<Z *>(std::malloc(sizeof(Z)));
  c_del();
  if (calls != 1) return 2;
  q = static_cast<Z *>(std::malloc(sizeof(Z)));
  c_dela();
  if (acalls != 1) return 3;
  v = std::malloc(4);
  c_delv();
  v = nullptr;
  c_delv();
  return calls == 2 ? 0 : 4;
}
