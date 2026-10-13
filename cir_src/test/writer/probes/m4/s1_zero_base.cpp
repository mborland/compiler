//options:--c++17
// (the S1 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S1: the zero-initialization of a base subobject is INIT_ZERO_BASE
// (the base's nvsize bytes: its tail padding may hold a member of the derived
// class), whole objects stay INIT_ZERO.  A value-initialized non-POD base with
// a trivial constructor (B() : the base entry has dik_zero), a dynamic base
// with an implicit constructor (zeroed first, then constructed), and new G().
inline void *operator new(unsigned long, void *p) noexcept { return p; }
struct B { int x; ~B() {} };
struct D : B { char c; D() : B() { c = 1; } };
struct BV { int x; virtual void f() {} };
struct DV : BV { char c; DV() : BV() { c = 2; } };
struct E { int x; E() = default; ~E() {} };
struct F : E { char c; F() : E() { c = 3; } };
struct G : B { G() = default; char c; };
alignas(16) char buf[64];
int main() {
  D *d = new (buf) D();
  DV *dv = new (buf) DV();
  F *f = new (buf) F();
  G *g = new (buf) G();
  return d->x + dv->x + f->x + g->x;
}
