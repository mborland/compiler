// S3 probe: member pointers end to end: constants with base offsets,
// virtual and non-virtual method pointers (with this adjustments), calls,
// conversions both ways, comparisons, bool, pointers in aggregates and arrays,
// local initialization from a constant that needs an adjustment.
#include <cstdio>
struct B1 { int b1; virtual int vf(int x) { return x + 1; } int nf(int x) { return x + 2; } };
struct B2 { int b2; int g(int x) { return x + 3; } virtual int vg(int x) { return x + 4; } };
struct D : B1, B2 { int d; int h(int x) { return x + 5; } int vg(int x) override { return x + 40; } };
typedef int (D::*DM)(int);
typedef int D::*DD;
struct S { DM m; DD p; int n; };
DM dm[] = { &D::h, &D::vf, &D::vg, static_cast<DM>(&B2::g), &D::nf, nullptr };
S gs = { &D::vg, &D::b2, 7 };
int main() {
  D d; d.b1 = 1; d.b2 = 2; d.d = 3;
  for (int i = 0; i < 5; ++i) std::printf("%d ", (d.*dm[i])(10));
  std::printf("\n%d %d %d\n", (d.*gs.m)(1), d.*gs.p, gs.n);
  DM l1 = static_cast<DM>(&B2::g); DM l2 = &D::vg; DM l3 = &D::h;
  S ls = { &D::nf, &D::d, 1 };
  std::printf("%d %d %d %d\n", (d.*l1)(1), (d.*l2)(1), (d.*l3)(1), (d.*ls.m)(1) + d.*ls.p);
  D *p = &d;
  std::printf("%d %d\n", (p->*l1)(2), p->*ls.p);
  p->*ls.p = 8; std::printf("%d\n", d.d);
  DD q = &B2::b2; int B2::*back = static_cast<int B2::*>(q);
  std::printf("%d %d %d %d\n", d.*q, d.*back, q == &D::b2, back != nullptr);
  int (B2::*bg)(int) = static_cast<int (B2::*)(int)>(l1); DM again = bg;
  std::printf("%d %d %d\n", (d.*again)(1), l1 == again, (bool)l1);
  DM nn = nullptr; DD nd = nullptr;
  std::printf("%d %d %d\n", !nn, !nd, nn == nullptr);
  return 0;
}
