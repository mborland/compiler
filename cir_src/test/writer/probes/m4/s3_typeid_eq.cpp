// S3 probe: type_info::operator== (inline in <typeinfo>, constexpr in C++23).
#include <cstdio>
#include <typeinfo>
struct A { virtual ~A() {} };
struct B : A { };
int main() {
  B b; A &a = b; A &c = b; A aa;
  std::printf("%d %d %d\n", typeid(a) == typeid(c), typeid(a) == typeid(aa), typeid(a) != typeid(aa));
  return 0;
}
