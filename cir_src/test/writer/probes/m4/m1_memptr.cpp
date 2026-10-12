// M4a probe: member pointers.  Data and function member pointers: constants,
// conversions (base to derived implicit, static_cast derived to base), virtual
// method pointers, calls, comparison, bool conversion, ->* and .*, member
// pointer to a member of a base with a this-adjustment in a global.
#include <cstdio>
struct B1 { int b1; virtual int vf(int x) { return x + 1; } int nf(int x) { return x + 2; } };
struct B2 { int b2; int g(int x) { return x + 3; } virtual int vg(int x) { return x + 4; } };
struct D : B1, B2 { int d; int h(int x) { return x + 5; } };
typedef int (D::*DM)(int);
typedef int D::*DD;
DD dd0 = &D::d;
DD dd1 = &D::b2;                       // B2 member as D member: implicit conversion, offset
DD ddn = nullptr;                      // null is -1
int B2::*bb = &B2::b2;
DD dd2 = bb;                           // base-to-derived conversion in a constant
DM dm[] = { &D::h, &D::vf, &D::vg, static_cast<DM>(&B2::g), &D::nf, nullptr };
int main() {
  D d; d.b1 = 1; d.b2 = 2; d.d = 3;
  std::printf("%d %d %d\n", d.*dd0, d.*dd1, d.*dd2);
  D *p = &d;
  for (int i = 0; i < 5; ++i) std::printf("%d ", (d.*dm[i])(10));
  std::printf("\n%d\n", (p->*dm[1])(1));
  std::printf("%d %d %d\n", ddn == nullptr, dd0 != ddn, (bool)dm[5]);
  int B2::*q = &B2::b2; DD r = q; int B2::*back = static_cast<int B2::*>(r);
  std::printf("%d %d\n", d.*r, d.*back);
  int (B2::*gm)(int) = &B2::g; DM dm2 = gm; std::printf("%d\n", (d.*dm2)(1));
  int (D::*vm)(int) = &D::vf; std::printf("%d %d\n", vm == dm[1], vm == dm[2]);
  return 0;
}
