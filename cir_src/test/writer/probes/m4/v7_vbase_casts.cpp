// M4a probe: IL shapes of conversions to virtual bases and through them:
// derived-to-virtual-base (direct and indirect), to a non-virtual base of a
// virtual base, member access through a virtual base, a virtual call on a
// virtual base, and the MEMINIT vbase entries of implicitly defined
// constructors/destructors.
#include <cstdio>
struct X { int x = 1; };
struct V : X { int v = 2; virtual void f() { std::puts("V::f"); } virtual ~V() {} };
struct A : virtual V { int a = 3; void f() { std::puts("A::f"); } };
struct B : virtual V { int b = 4; };
struct D : A, B { int d = 5; };            // V is a virtual base of A and B
struct E : D { int e = 6; };
struct W { W() { std::puts("W()"); } virtual ~W() {} };
struct F : virtual W { };                   // implicit ctor/dtor with a vbase
struct G : F { };                           // implicit ctor: vbase W is G's to construct
int main() {
  E e; D &d = e; V &v = e; X &x = e;
  std::printf("%d %d %d %d\n", e.v, d.x, v.v, x.x);
  A *pa = &e; V *pv = pa; pv->f();
  G g; F &f = g; (void)f;
  return 0;
}
