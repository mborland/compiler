// M4a probe: number.virtual_function semantics (slot numbers).
// A: f 0, ~A 1 (+2 deleting), g 3.  B: f reuses 0, implicit ~B reuses 1,
// h2 new 4.  C: ~C 1, g 3.  D0: covariant, same slot.  D2: covariant over a
// virtual primary base.  Z: overriders of a non-primary base get new slots.
#include <cstdio>
struct A { virtual void f() { std::puts("A::f"); } virtual ~A() {}
           virtual void g(int) { std::puts("A::g"); } };
struct B : A { void f() { std::puts("B::f"); } void h() {}
               virtual void h2() { std::puts("B::h2"); } };
struct C : B { ~C() { std::puts("~C"); } void g(int) { std::puts("C::g"); } };
struct R0 { virtual R0 *clone() { return new R0; } virtual ~R0() {} };
struct D0 : R0 { D0 *clone() { return new D0; } };
struct RB { virtual RB *get() { return this; } };
struct B2 : virtual RB { };
struct RD : RB { };
struct D2 : B2 { RD *get() { return 0; } };
struct X { virtual void g() { std::puts("X::g"); } };
struct Y { virtual void h() { std::puts("Y::h"); } };
struct Z : X, Y { void g() { std::puts("Z::g"); } void h() { std::puts("Z::h"); }
                  virtual void z() { std::puts("Z::z"); } };
// pure and deleted virtual functions
struct P { virtual void p() = 0; virtual void d() = delete; };
struct Q : P { void p() { std::puts("Q::p"); } void d() = delete; };
int main() {
  C c; A &a = c; a.f(); a.g(1); static_cast<B&>(c).h2();
  D0 d0; R0 &r = d0; delete r.clone();
  Z z; X &x = z; Y &y = z; x.g(); y.h(); z.z();
  Q q; q.p();
  D2 d2; RB &rb = d2; (void)rb.get();
  return 0;
}
