// M4a probe: IL shapes of virtual calls: through a reference, a pointer, an
// object (no dispatch), a qualified name (no dispatch), an explicit destructor
// call, a call from a constructor and a destructor, a final function, a call
// through a base that does not declare the function (this adjustment).
#include <cstdio>
struct A { virtual void f() { std::puts("A::f"); } virtual ~A() { std::puts("~A"); } };
struct B { virtual void g() { std::puts("B::g"); } virtual void h() final { std::puts("B::h"); } };
struct D : A, B {
  D() { f(); g(); }                      // virtual calls in a constructor
  ~D() { f(); std::puts("~D"); }
  void f() { std::puts("D::f"); }
  void g() { std::puts("D::g"); }
};
void calls(D &d, D *pd, A &a, B *pb) {
  d.f(); pd->g(); a.f(); pb->g(); pb->h();   // d.f(): object expression is a reference
  d.A::f(); pd->D::g();                      // qualified: no dispatch
  a.~A();                                    // explicit destructor call
}
struct E : A { };                          // does not declare f: the selector is cast to A
void callsE(E &e, E *pe) { e.f(); pe->f(); }
// A using-declaration names a base's function in a class whose base overrides it:
// the plain call (object of known class) calls the final overrider in that class.
struct UA { virtual void f() { std::puts("UA::f"); } };
struct UB : UA { void f() override { std::puts("UB::f"); } };
struct UC : UB { using UA::f; };
void usingcall(UC &r) { UC c; c.f(); r.f(); }
// A final class: calls on it are plain; a function declared in a base and not
// overridden stays the base's.
struct FA { virtual void g() { std::puts("FA::g"); } virtual void h() { std::puts("FA::h"); } };
struct FB final : FA { void g() override { std::puts("FB::g"); } };
void fin(FB *p, FB &r, FB o) { p->g(); r.g(); p->h(); o.h(); o.g(); }
// Covariant return: the final overrider of CA::f in CC (CB::f) returns a
// different type, so clang keeps the dispatch for c.f(); the call that names
// CB::f (through CD) is plain.
struct R0 { virtual ~R0() {} };
struct R1 : R0 {};
struct CA { virtual R0 *f() { return new R0; } virtual ~CA() {} };
struct CB : CA { R1 *f() override { return new R1; } };
struct CC : CB { using CA::f; };
struct CD : CB { };
int covcall(CC &r) {
  CC c; CD d;
  R0 *a = c.f(), *b = r.f(), *e = d.f();
  int n = (a != 0) + (b != 0) + (e != 0);
  delete a; delete b; delete e;
  return n;
}
int main() {
  D d; calls(d, &d, d, &d);
  E e; callsE(e, &e);
  UC uc; usingcall(uc);
  FB fb; fin(&fb, fb, fb);
  CC cc; (void)covcall(cc);
  D local; local.f();                       // an object: no dispatch
  return 0;
}
