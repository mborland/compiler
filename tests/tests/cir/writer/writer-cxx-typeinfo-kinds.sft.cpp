//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Typeinfo of types that are not classes (TYPEINFO_DEF): fundamental types and
// pointers to cv-unqualified or const fundamental types are external
// declarations (the C++ runtime has them); a volatile pointee, pointers to
// pointers or classes, member pointers, functions, arrays, enums, scoped enums
// are linkonce_odr definitions in a COMDAT; the masks (CONST, VOLATILE,
// NOEXCEPT) come from the pointee, and a pointer's typeinfo comes after its
// pointee's.
namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;
  bool operator==(const type_info &) const;
  bool operator!=(const type_info &) const;
};
}
struct A { virtual ~A(); };
struct B : A { };
enum E { e0 };
enum class EC : short { c0 };
const std::type_info *all[] = {
  &typeid(const int *), &typeid(int *const), &typeid(int A::*),
  &typeid(int (A::*)()), &typeid(void (*)(int, ...)), &typeid(int[3]),
  &typeid(const char (*)[4]), &typeid(E), &typeid(EC), &typeid(void *),
  &typeid(volatile int *), &typeid(A **), &typeid(const A *const *),
  &typeid(int B::*const *), &typeid(void (A::*)() const),
  &typeid(unsigned long long), &typeid(wchar_t), &typeid(char16_t),
  &typeid(decltype(nullptr)), &typeid(__int128), &typeid(long double),
  &typeid(bool), &typeid(void (*)() noexcept), &typeid(A *), &typeid(const A *),
  &typeid(int A::*const *), &typeid(void), &typeid(char *), &typeid(const void *)
};
int use() { return all[0] == 0; }
