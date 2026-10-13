// The host side of cxx-s3-writer-typeinfo.test: the typeinfo objects of the
// CIR unit (classes, pointers, pointers to members, a function pointer type,
// fundamental types) compared with this compiler's.
#include <cxxabi.h>
#include <cstring>
#include <typeinfo>

struct P { int x; };
struct Q : P { int y; };
struct R { virtual ~R() {} };

const std::type_info *f1();
const std::type_info *f2();
const std::type_info *f3();
const std::type_info *f4();
const std::type_info *f5();
const std::type_info *f6();
const std::type_info *f7();
const std::type_info *f8();

int main()
{
  if (!(*f1() == typeid(P)) || !(*f2() == typeid(Q))) return 1;
  if (!(*f3() == typeid(P *)) || !(*f4() == typeid(const Q *const *))) return 2;
  if (!(*f5() == typeid(int P::*)) || !(*f6() == typeid(R)) ||
      !(*f7() == typeid(double)) || !(*f8() == typeid(void (P::*)(int))))
    return 3;
  if (std::strcmp(f4()->name(), typeid(const Q *const *).name()) != 0) return 4;
  if (std::strcmp(f8()->name(), typeid(void (P::*)(int)).name()) != 0) return 5;
  if (dynamic_cast<const __cxxabiv1::__si_class_type_info *>(f2()) == nullptr ||
      static_cast<const __cxxabiv1::__si_class_type_info *>(f2())->__base_type !=
          &typeid(P))
    return 6;
  const auto *ptr = dynamic_cast<const __cxxabiv1::__pointer_type_info *>(f4());
  if (ptr == nullptr || (ptr->__flags & ptr->__const_mask) == 0 ||
      *ptr->__pointee != typeid(const Q *))
    return 7;
  if (dynamic_cast<const __cxxabiv1::__pointer_to_member_type_info *>(f5()) ==
          nullptr ||
      static_cast<const __cxxabiv1::__pointer_to_member_type_info *>(f5())
              ->__context != &typeid(P))
    return 8;
  return 0;
}
