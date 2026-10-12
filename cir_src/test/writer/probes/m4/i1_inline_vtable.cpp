// M4a probe: "used" marker for a vtable: when is the vtable needed?
// A constructor body (VPTR_INIT), an aggregate constant (C_VPTR), a derived
// class vtable referencing a base's functions, a class whose ctor is never
// instantiated (no vtable), an explicit instantiation.  Also the availability
// (tim_used / defined) of the virtual members that a vtable references.
#include <cstdio>
struct N1 { virtual void f(); virtual void g() { std::puts("N1::g"); } };       // key f: not defined here
struct U1 { virtual void f() { std::puts("U1::f"); } };                       // used: object created
struct U2 { virtual void f() { std::puts("U2::f"); } };                       // never used
struct C1 { constexpr C1() {} virtual void f() const {} };
constexpr C1 c1;                                                              // constant with C_VPTR
struct B1 { virtual void f() { std::puts("B1::f"); } virtual ~B1() {} };
struct D1 : B1 { void f() { std::puts("D1::f"); } };                          // D1::~D1 implicit
struct I1 { virtual void f() { std::puts("I1::f"); } };                      // inline in class
template <class T> struct Tm { virtual void f() { std::puts("Tm::f"); } virtual void h() { T t; (void)t; } };
template struct Tm<int>;
int main() {
  U1 u; D1 d; B1 &b = d; b.f(); u.f(); I1 *p = new I1; p->f(); delete p;
  Tm<char> tc; (void)tc; (void)c1;
  N1 *pn = nullptr; if (pn) pn->g();
  return 0;
}
