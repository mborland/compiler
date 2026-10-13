// The host side of cxx-s3-rtti.test: compares the typeinfo objects of the CIR
// unit (std::type_info classes of the C++ runtime) and the results of its
// dynamic_cast and typeid with this compiler's.
#include <cxxabi.h>
#include <cstring>
#include <typeinfo>

struct A { virtual ~A() {} int a; };
struct B : A { int b; };
struct C : virtual A { int c; };
struct M1 : A { int m1; };
struct M2 : A { int m2; };
struct Both : M1, M2 { int both; };
struct Priv : private A { int p; };
enum E { e1 };

A *make_a();
A *make_b();
A *make_c();
M1 *make_both();
Priv *make_priv();

extern "C" {
B *dyn_b(A *);
C *dyn_c(A *);
M2 *cross(M1 *);
Priv *dyn_priv(A *);
void *dyn_void(A *);
B *dyn_ref(A *);
const std::type_info *tid_dyn(A *);
const std::type_info *tid_dyn_ref(A *);
const std::type_info *tid_a();
const std::type_info *tid_b();
const std::type_info *tid_int();
const std::type_info *tid_pi();
const std::type_info *tid_ppi();
const std::type_info *tid_pfn();
const std::type_info *tid_arr();
const std::type_info *tid_enum();
const std::type_info *tid_ptm();
}

int main()
{
  A *a = make_a();
  A *b = make_b();
  A *c = make_c();
  M1 *both = make_both();

  if (dyn_b(b) != dynamic_cast<B *>(b) || dyn_b(b) == nullptr) return 1;
  if (dyn_b(a) != nullptr || dyn_b(c) != nullptr || dyn_b(nullptr) != nullptr)
    return 2;
  if (dyn_c(c) != dynamic_cast<C *>(c) || dyn_c(c) == nullptr) return 3;
  if (dyn_c(a) != nullptr || dyn_c(b) != nullptr) return 4;
  if (cross(both) != dynamic_cast<M2 *>(both) || cross(both) == nullptr)
    return 5;
  if (dyn_priv(a) != nullptr || dyn_priv(b) != nullptr) return 6;
  if (dyn_void(b) != dynamic_cast<void *>(b) || dyn_void(b) != static_cast<B *>(static_cast<A *>(b))) return 7;
  if (dyn_void(c) != dynamic_cast<void *>(c) || dyn_void(nullptr) != nullptr) return 8;
  if (dyn_ref(b) != dynamic_cast<B *>(b)) return 9;
  try {
    dyn_ref(a);
    return 10;
  } catch (const std::bad_cast &) {
  }
  if (*tid_dyn(b) != typeid(B) || *tid_dyn(c) != typeid(C) || *tid_dyn(a) != typeid(A))
    return 11;
  if (*tid_dyn_ref(b) != typeid(B)) return 12;
  try {
    tid_dyn(nullptr);
    return 13;
  } catch (const std::bad_typeid &) {
  }
  if (*tid_a() != typeid(A) || *tid_b() != typeid(B)) return 14;
  if (tid_a() != &typeid(A)) return 15;   /* the same object: the vtable's */
  if (*tid_int() != typeid(int) || *tid_pi() != typeid(int *)) return 16;
  if (*tid_ppi() != typeid(int **) || *tid_pfn() != typeid(void (*)(int)))
    return 17;
  if (*tid_arr() != typeid(int[3]) || *tid_enum() != typeid(E) ||
      *tid_ptm() != typeid(int A::*))
    return 18;
  if (*tid_ppi() == *tid_pi() || *tid_arr() == *tid_int()) return 19;
  if (std::strcmp(tid_ppi()->name(), typeid(int **).name()) != 0 ||
      std::strcmp(tid_ptm()->name(), typeid(int A::*).name()) != 0 ||
      std::strcmp(tid_arr()->name(), typeid(int[3]).name()) != 0)
    return 20;
  /* The runtime classes the typeinfo objects are instances of. */
  if (dynamic_cast<const __cxxabiv1::__pointer_type_info *>(tid_ppi()) ==
          nullptr ||
      dynamic_cast<const __cxxabiv1::__pointer_to_member_type_info *>(
          tid_ptm()) == nullptr ||
      dynamic_cast<const __cxxabiv1::__function_type_info *>(
          static_cast<const __cxxabiv1::__pointer_type_info *>(tid_pfn())
              ->__pointee) == nullptr ||
      dynamic_cast<const __cxxabiv1::__array_type_info *>(tid_arr()) == nullptr ||
      dynamic_cast<const __cxxabiv1::__enum_type_info *>(tid_enum()) == nullptr)
    return 21;
  if (static_cast<const __cxxabiv1::__pointer_to_member_type_info *>(tid_ptm())
              ->__context != &typeid(A) ||
      static_cast<const __cxxabiv1::__pointer_type_info *>(tid_ppi())->__pointee !=
          &typeid(int *))
    return 22;
  return 0;
}
