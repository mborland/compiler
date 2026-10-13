// The host side of cxx-s3-incomplete-typeinfo.test: typeinfo of pointers to
// an incomplete class (internal in the CIR unit) and typeinfo addresses in
// constant initializers.
#include <cxxabi.h>
#include <cstring>
#include <typeinfo>

struct I;
struct P { int x; };
struct Q : P { int y; };
struct A { virtual ~A() {} };
struct H { const std::type_info *t; int n; };

const std::type_info *i1();
const std::type_info *i2();
const std::type_info *i3();
const std::type_info *i4();
extern const std::type_info *g_a;
extern const std::type_info *g_p;
extern const std::type_info *g_q;
extern const std::type_info *g_int;
extern const std::type_info *g_pi;
extern const std::type_info *g_arr[3];
extern H g_h;

int main()
{
  if (!(*i1() == typeid(I *)) || !(*i2() == typeid(I **)) ||
      !(*i3() == typeid(const I *)) || !(*i4() == typeid(int I::*)))
    return 1;
  if (std::strcmp(i1()->name(), typeid(I *).name()) != 0) return 2;
  /* The typeinfo of a pointer to an incomplete class is internal and its
     name has no '*' (it is not TU-local in the name's sense). */
  if (i1()->name()[0] == '*') return 3;
  const auto *ptr = dynamic_cast<const __cxxabiv1::__pointer_type_info *>(i1());
  if (ptr == nullptr || (ptr->__flags & ptr->__incomplete_mask) == 0) return 4;
  if (!(*g_a == typeid(A)) || !(*g_p == typeid(P)) || !(*g_q == typeid(Q)) ||
      !(*g_int == typeid(int)) || !(*g_pi == typeid(I *)))
    return 5;
  if (!(*g_arr[0] == typeid(A)) || !(*g_arr[1] == typeid(double)) ||
      !(*g_arr[2] == typeid(Q *)))
    return 6;
  if (!(*g_h.t == typeid(P)) || g_h.n != 3) return 7;
  if (g_a != &typeid(A) || g_p != &typeid(P)) return 8;
  return 0;
}
