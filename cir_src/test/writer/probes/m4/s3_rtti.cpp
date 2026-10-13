// S3 probe: typeid (static and dynamic forms, null check), dynamic_cast
// (pointer, reference, void *, cross cast, virtual base), run against g++.
#include <cstdio>
#include <typeinfo>
struct A { virtual ~A() {} int a = 1; };
struct B : A { int b = 2; };
struct C { virtual ~C() {} int c = 3; };
struct D : B, C { int d = 4; };
struct V { virtual ~V() {} };
struct X : virtual V { int x = 5; };
struct Y : virtual V { int y = 6; };
struct Z : X, Y { int z = 7; };
struct N { int n; };
int main() {
  D d; A *pa = &d; B *pb = &d; C *pc = &d;
  std::printf("%d %d %d\n", typeid(*pa) == typeid(D), typeid(*pb) == typeid(D), typeid(*pc) == typeid(D));
  std::printf("%d %d\n", typeid(d) == typeid(D), typeid(int) == typeid(int));
  std::printf("%d %d\n", typeid(*pa) == typeid(B), typeid(A) == typeid(*pa));
  std::printf("%s %s\n", typeid(*pa).name(), typeid(int).name());
  B *b1 = dynamic_cast<B *>(pa);
  C *c1 = dynamic_cast<C *>(pa);
  D *d1 = dynamic_cast<D *>(pc);
  void *v1 = dynamic_cast<void *>(pc);
  std::printf("%d %d %d %d\n", b1 == pb, c1 == pc, d1 == &d, v1 == (void *)&d);
  A a; A *pa2 = &a;
  std::printf("%d %d\n", dynamic_cast<B *>(pa2) == nullptr, dynamic_cast<C *>(pa2) == nullptr);
  { B &r = dynamic_cast<B &>(*pa); std::printf("%d\n", r.b); }
  Z z; V *pv = &z; X *px = &z; Y *py = &z;
  std::printf("%d %d %d\n", dynamic_cast<Z *>(pv) == &z, dynamic_cast<Y *>(px) == py, dynamic_cast<X *>(py) == px);
  A *np = nullptr;
  std::printf("%d\n", dynamic_cast<B *>(np) == nullptr);
  return 0;
}
