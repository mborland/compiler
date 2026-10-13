//options: --c++17 --g++ --gnu_version=140000
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: devirtualization as clang does it (spec 12.6): a call on a
// complete object, or on an object of a final class (after the front end's
// conversion to the class that declares the function is looked through), or
// of a final function, is a plain call of the final overrider of the named
// function in the object's class; a using-declaration names a base's function
// that a base overrides (UC); a covariant final overrider keeps the dispatch
// (CC).
extern "C" int printf(const char *, ...);
struct A { virtual void f() { printf("A::f\n"); } virtual ~A() { printf("~A\n"); } };
struct B { virtual void g() { printf("B::g\n"); } virtual void h() final { printf("B::h\n"); } };
struct D : A, B {
  D() { f(); g(); }
  ~D() { f(); }
  void f() { printf("D::f\n"); }
  void g() { printf("D::g\n"); }
};
void calls(D &d, D *pd, A &a, B *pb) {
  d.f(); pd->g(); a.f(); pb->g(); pb->h();
  d.A::f(); pd->D::g();
  a.~A();
}
struct E : A {};
void callsE(E &e, E *pe) { e.f(); pe->f(); }
struct UA { virtual void f() { printf("UA::f\n"); } };
struct UB : UA { void f() override { printf("UB::f\n"); } };
struct UC : UB { using UA::f; };
void usingcall(UC &r) { UC c; c.f(); r.f(); }
struct FA { virtual void g() { printf("FA::g\n"); } virtual void h() { printf("FA::h\n"); } };
struct FB final : FA { void g() override { printf("FB::g\n"); } };
void fin(FB *p, FB &r, FB o) { p->g(); r.g(); p->h(); o.h(); o.g(); }
struct R0 { virtual ~R0() {} };
struct R1 : R0 {};
struct CA { virtual R0 *f() { return new R0; } virtual ~CA() {} };
struct CB : CA { R1 *f() override { return new R1; } };
struct CC : CB { using CA::f; };
struct CD : CB {};
int covcall(CC &r) {
  CC c;
  CD d;
  R0 *a = c.f(), *b = r.f(), *e = d.f();
  int n = (a != 0) + (b != 0) + (e != 0);
  delete a; delete b; delete e;
  return n;
}
int main() {
  D d;
  calls(d, &d, d, &d);
  E e;
  callsE(e, &e);
  UC uc;
  usingcall(uc);
  FB fb;
  fin(&fb, fb, fb);
  CC cc;
  (void)covcall(cc);
  D local;
  local.f();
  return 0;
}
