//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Constants of member pointers: a data member's offset in the complete class
// (the member's own offset plus a base subobject's: the implicit conversion
// of a pointer to a member of a base), null (nil), a non-virtual function
// (g, adjustment), a virtual one (VIRTUAL, slot from CLASS_VFUNCS) and a
// function of a second base with a this adjustment (valid in a VAR_INIT).
// In a body the constant is CONST_DATA_MEMBER / CONST_METHOD and a STORE.
struct B1 { int b1; virtual int vf(int x); int nf(int x); };
struct B2 { int b2; int g(int x); virtual int vg(int x); };
struct D : B1, B2 { int d; int h(int x); };
typedef int (D::*DM)(int);
typedef int D::*DD;
DD dd0 = &D::d;
DD dd1 = &D::b2;
DD ddn = nullptr;
int B2::*bb = &B2::b2;
DM dm[] = { &D::h, &D::vf, &D::vg, static_cast<DM>(&B2::g), &D::nf, nullptr };
struct S { DM m; DD p; int n; };
S gs = { &D::vg, &D::b2, 7 };
int f() {
  DD a = &D::d;
  DD b = &D::b1;
  DD c = nullptr;
  DM m1 = &D::h;
  DM m2 = &D::vf;
  DM m3 = &D::vg;
  DM m4 = static_cast<DM>(&B2::g);
  DM m5 = nullptr;
  return (a == b) + (c == 0) + (m1 == m2) + (m3 == m4) + (m5 == 0);
}
