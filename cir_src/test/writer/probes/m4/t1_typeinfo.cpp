// M4a probe: typeinfo: typeid static/dynamic, dynamic_cast ptr/ref/void,
// typeinfo of fundamental, pointer, member pointer, function, array, enum,
// class types; TU-local classes; the per-TU closure (thrown, caught,
// typeid'd, base types).
#include <cstdio>
#include <typeinfo>
struct A { virtual ~A() {} };
struct B : virtual A { };
struct C : virtual A { int c; };
struct D : B, C { };
struct I;                                   // incomplete
enum E { e0 };
namespace { struct Hidden : A { }; }
void fn(int) {}
int main() {
  D d; A &a = d; A *pa = &d;
  std::printf("%d\n", typeid(a) == typeid(D));
  std::printf("%d\n", typeid(int) == typeid(int));
  std::printf("%d %d\n", typeid(const int *) == typeid(int *), typeid(int *) == typeid(int *));
  std::printf("%d\n", typeid(I *) == typeid(I *));
  std::printf("%d %d %d %d\n", typeid(int D::*) == typeid(int C::*), typeid(void (*)(int)) == typeid(decltype(&fn)), typeid(int[3]) == typeid(int[3]), typeid(E) == typeid(E));
  std::printf("%d\n", dynamic_cast<C *>(pa) != nullptr);
  std::printf("%d\n", dynamic_cast<void *>(pa) == (void *)&d);
  try { (void)dynamic_cast<B &>(a); (void)dynamic_cast<Hidden &>(a); } catch (std::bad_cast &) { std::puts("bad_cast"); }
  A *np = nullptr;
  try { (void)typeid(*np); } catch (std::bad_typeid &) { std::puts("bad_typeid"); }
  Hidden h; A &ah = h; std::printf("%d\n", typeid(ah) == typeid(Hidden));
  return 0;
}
