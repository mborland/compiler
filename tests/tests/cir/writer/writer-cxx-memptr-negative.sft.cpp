//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Member pointer forms the writer does not support are diagnosed, and the
// function is dropped, not written wrong: an aggregate local to a function
// whose element is a method pointer that needs a this adjustment (a C_METHOD
// with a non-zero adjustment is valid only in a VAR_INIT; a scalar local uses
// CONST_METHOD and a STORE and is fine, as is the same constant in a global).
struct B1 { int b1; int f(int); };
struct B2 { int b2; int g(int); };
struct D : B1, B2 { int d; };
typedef int (D::*DM)(int);
struct SM { DM m; int n; };
SM gs = { static_cast<DM>(&B2::g), 1 };
int ok1() { DM m = static_cast<DM>(&B2::g); return m == nullptr; }
int bad1() {
  DM arr[2] = { static_cast<DM>(&B2::g), nullptr };
  return arr[0] == nullptr;
}
int bad2() {
  SM s = { static_cast<DM>(&B2::g), 1 };
  return s.n;
}
int ok2() {
  SM s = { &D::f, 1 };
  return s.n;
}
