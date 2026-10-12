// M4a probe: where the FE zero-initializes a base subobject: value-initialization
// of a non-POD base (D() : B() {}) whose implicit constructor is trivial; and
// the base of a value-initialized derived class.
#include <cstdio>
#include <new>
struct B { int x; ~B() {} };                       // non-POD (destructor), trivial default constructor
struct D : B { char c; D() : B() { c = 1; } };      // B() zero-initializes the base: only B's nvsize bytes
struct BV { int x; virtual void f() {} };           // dynamic base
struct DV : BV { char c; DV() : BV() { c = 2; } };  // value-init of a dynamic base (non-trivial ctor): zero first?
struct E { int x; E() = default; ~E() {} };         // defaulted default constructor
struct F : E { char c; F() : E() { c = 3; } };
struct G : B { G() = default; char c; };            // G() value-initializes: the base is zeroed too
struct M { long M::*pm; ~M() {} };                  // base with a data-member pointer
struct DM : M { char c; DM() : M() { c = 4; } };
int main() {
  alignas(16) char buf[64];
  for (int i = 0; i < 64; i++) buf[i] = (char)0x55;
  D *d = new (buf) D(); std::printf("%d %d\n", d->x, d->c);
  for (int i = 0; i < 64; i++) buf[i] = (char)0x55;
  DV *dv = new (buf) DV(); std::printf("%d %d\n", dv->x, dv->c);
  for (int i = 0; i < 64; i++) buf[i] = (char)0x55;
  F *f = new (buf) F(); std::printf("%d %d\n", f->x, f->c);
  for (int i = 0; i < 64; i++) buf[i] = (char)0x55;
  G *g = new (buf) G(); std::printf("%d %d\n", g->x, g->c);
  for (int i = 0; i < 64; i++) buf[i] = (char)0x55;
  DM *dm = new (buf) DM(); std::printf("%d %d\n", dm->pm == nullptr, dm->c);
  return 0;
}
