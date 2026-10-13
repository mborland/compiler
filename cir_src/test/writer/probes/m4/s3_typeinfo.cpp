// S3 probe: typeid of non-class types in function bodies (the TYPEINFO_DEF
// objects the library defines): names, equality, before(); compared with g++.
#include <cstdio>
#include <typeinfo>
struct A { virtual ~A() {} int m; void f(); };
struct B : A { };
enum E { e0 };
enum class EC : short { c0 };
namespace { struct Hid { virtual ~Hid() {} int x; }; }
#define SHOW(T) std::printf("%s\n", typeid(T).name())
int main() {
  SHOW(int); SHOW(const int *); SHOW(int *const); SHOW(int A::*); SHOW(int (A::*)());
  SHOW(void (*)(int)); SHOW(int[3]); SHOW(const char (*)[4]); SHOW(E); SHOW(EC);
  SHOW(void *); SHOW(volatile int *); SHOW(A **); SHOW(const A *const *); SHOW(B);
  SHOW(unsigned long long); SHOW(wchar_t); SHOW(char16_t); SHOW(__int128); SHOW(long double);
  SHOW(bool); SHOW(void (*)() noexcept); SHOW(A *); SHOW(const A *);
  std::printf("%d %d %d\n", typeid(int *) == typeid(int *), typeid(const int *) == typeid(int *),
              typeid(A *) == typeid(B *));
  std::printf("%d %d\n", typeid(int A::*) == typeid(int A::*), typeid(E) == typeid(EC));
  return 0;
}
