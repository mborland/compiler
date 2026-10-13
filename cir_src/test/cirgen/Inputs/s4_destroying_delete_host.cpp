// Host of cxx-s4-destroying-delete.test: destroying operator delete functions
// (C++20) called by DELETE with the DESTROYING flag.  The operators only
// record their arguments; the CIR functions c_* contain the delete
// expressions.
#include <cstddef>
#include <cstdio>
#include <new>

struct S { int v; static void operator delete(S *, std::destroying_delete_t); };
struct S2 {
  int v;
  static void operator delete(S2 *, std::destroying_delete_t, std::size_t);
};
struct alignas(16) U {
  int v;
  static void operator delete(U *, std::destroying_delete_t, std::size_t,
                              std::align_val_t);
};
extern "C" void c_s(S *);
extern "C" void c_s2(S2 *);
extern "C" void c_u(U *);

static void *g_ptr;
static std::size_t g_size, g_align;
static int g_calls;
void S::operator delete(S *p, std::destroying_delete_t) {
  g_ptr = p; ++g_calls;
}
void S2::operator delete(S2 *p, std::destroying_delete_t, std::size_t n) {
  g_ptr = p; g_size = n; ++g_calls;
}
void U::operator delete(U *p, std::destroying_delete_t, std::size_t n,
                        std::align_val_t a) {
  g_ptr = p; g_size = n; g_align = static_cast<std::size_t>(a); ++g_calls;
}

int main() {
  S s;
  S2 s2;
  U u;

  c_s(&s);
  if (g_ptr != &s || g_calls != 1) return 1;
  g_ptr = nullptr;
  c_s(nullptr);
  if (g_ptr != nullptr || g_calls != 1) return 2;
  c_s2(&s2);
  if (g_ptr != &s2 || g_size != sizeof(S2) || g_calls != 2) return 3;
  c_u(&u);
  if (g_ptr != &u || g_size != 16 || g_align != 16 || g_calls != 3) return 4;
  c_u(nullptr);
  if (g_calls != 3) return 5;
  std::printf("ok\n");
  return 0;
}
