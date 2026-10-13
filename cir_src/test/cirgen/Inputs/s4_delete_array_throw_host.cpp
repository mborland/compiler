// Host of cxx-s4-delete-array-throw.test: delete[] of T[n] where an element
// destructor throws.  The remaining elements are destroyed and the storage
// is freed anyway (g++ and clang do the same).
#include <cstdio>
#include <cstdlib>
struct T { ~T() noexcept(false); int x; };
struct Un { ~Un() noexcept; int x; };
Un::~Un() noexcept {}
extern "C" void c_del(T *p);
extern "C" void c_del_nothrow(Un *p);
static T *g_victim;
static int g_dtors, g_freed;
static void *g_block;
T::~T() noexcept(false) {
  ++g_dtors;
  if (this == g_victim)
    throw 1;
}
void operator delete[](void *p) noexcept {
  ++g_freed;
  if (p != g_block)
    std::abort();
  std::free(p);
}
static int run(int n, int victim) {
  /* An Itanium array cookie: the element count in the word before the
     elements; operator delete[] gets the address of the cookie. */
  char *block = static_cast<char *>(std::malloc(8 + n * sizeof(T)));
  T *a = reinterpret_cast<T *>(block + 8);
  int thrown = 0;
  g_block = block;
  *reinterpret_cast<unsigned long *>(block) = n;
  g_victim = victim < 0 ? nullptr : a + victim;
  g_dtors = 0;
  g_freed = 0;
  try {
    c_del(a);
  } catch (int) {
    thrown = 1;
  }
  std::printf("n=%d victim=%d thrown=%d dtors=%d freed=%d\n", n, victim,
              thrown, g_dtors, g_freed);
  return (thrown != (victim >= 0) ? 1 : 0) | (g_dtors != n ? 2 : 0) |
         (g_freed != 1 ? 4 : 0);
}
int main() {
  int bad = 0;
  bad |= run(1, 0);
  bad |= run(3, 1);
  bad |= run(3, 2);
  bad |= run(3, -1);
  return bad;
}
