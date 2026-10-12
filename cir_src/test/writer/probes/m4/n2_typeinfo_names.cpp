// M4a probe: Itanium typeinfo objects for non-class types (what a throw,
// catch or typeid of the type makes the TU define or declare).
#include <typeinfo>
struct A { virtual ~A() {} };
struct B : A { };
struct I;
enum E { e0 };
enum class EC : short { c0 };
namespace { struct Hid { int x; }; enum HE { h0 }; }
void fn(int, ...) {}
const std::type_info *all[] = {
  &typeid(const int *), &typeid(int *const), &typeid(int A::*), &typeid(int (A::*)()), &typeid(void (*)(int, ...)),
  &typeid(int[3]), &typeid(const char (*)[4]), &typeid(E), &typeid(EC), &typeid(I *), &typeid(const I *), &typeid(void *),
  &typeid(volatile int *), &typeid(A **), &typeid(const A *const *), &typeid(int B::*const *), &typeid(void (A::*)() const),
  &typeid(unsigned long long), &typeid(wchar_t), &typeid(char16_t), &typeid(decltype(nullptr)), &typeid(__int128), &typeid(long double),
  &typeid(bool), &typeid(float _Complex), &typeid(Hid *), &typeid(HE), &typeid(void (*)() noexcept), &typeid(A *), &typeid(const A *),
  &typeid(int A::*const *), &typeid(void)
};
int main() { return all[0] == 0; }
