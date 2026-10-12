// M4a probe: per-base-entry overrider lists.  Repeated non-virtual base
// (two entries for A at different offsets, each with its own list), a
// diamond with virtual bases, and an overrider in an intermediate class.
#include <cstdio>
struct A { virtual void f() { std::puts("A::f"); } virtual void g() { std::puts("A::g"); } int a; };
struct B : A { void f() { std::puts("B::f"); } int b; };
struct C : A { void g() { std::puts("C::g"); } int c; };
struct D : B, C { void f() { std::puts("D::f"); } int d; };   // repeated A
struct V { virtual void v() { std::puts("V::v"); } virtual void w() { std::puts("V::w"); } };
struct L : virtual V { void v() { std::puts("L::v"); } };
struct R : virtual V { void w() { std::puts("R::w"); } };
struct J : L, R { void v() { std::puts("J::v"); } void w() { std::puts("J::w"); } };
// overrider in an intermediate virtual base's primary chain
struct Base { virtual void m() { std::puts("Base::m"); } };
struct Ent : virtual Base { void m() { std::puts("Ent::m"); } };
struct Msg : virtual Ent { };
struct Msg2 : Msg { void m() { std::puts("Msg2::m"); } };
int main() {
  D d; B &b = d; C &c = d;
  static_cast<A&>(b).f(); static_cast<A&>(b).g();
  static_cast<A&>(c).f(); static_cast<A&>(c).g();
  J j; V &v = j; v.v(); v.w();
  Msg2 m; Base &bs = m; bs.m();
  return 0;
}
