// Host of cxx-s4-method-pointer-lowered-aggregates.test: globals that hold
// pointers to member functions with a this-adjustment, in records, nested
// arrays and unions.  The values are compared with g++'s own.
#include <cstdio>
struct B1 { int x; int f(int); };
struct B2 { int y; int g(int); };
struct D : B1, B2 { int d; };
typedef int (D::*DM)(int);
struct H { int k; DM pm; int tail; };
struct G { H a[2]; };
union W { DM pm; long x; };
extern H gh;
extern G gg;
extern H garr[2];
extern W gu;
int B1::f(int a) { return x + a; }
int B2::g(int a) { return y * 10 + a; }

int main() {
  D d;
  DM pf = &B1::f;
  DM pg = &B2::g;
  int bad = 0;

  d.x = 1; d.y = 2; d.d = 3;
  if (gh.k != 1 || gh.tail != 3 || gh.pm != pg || (d.*gh.pm)(5) != 25) bad |= 1;
  if (gg.a[0].k != 7 || gg.a[0].pm != pf || (d.*gg.a[0].pm)(5) != 6 ||
      gg.a[0].tail != 8) bad |= 2;
  if (gg.a[1].k != 9 || gg.a[1].pm != pg || (d.*gg.a[1].pm)(5) != 25 ||
      gg.a[1].tail != 0) bad |= 4;
  if (garr[0].k != 4 || garr[0].pm != pg || garr[1].k != 5 ||
      garr[1].pm != pf || (d.*garr[1].pm)(1) != 2) bad |= 8;
  if (gu.pm != pg || (d.*gu.pm)(5) != 25) bad |= 16;
  std::printf("bad=%d\n", bad);
  return bad;
}
