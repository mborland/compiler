//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct M { int v; M(); M(int); ~M(); };
void f() {
  M a[3];
  M b[2] = { M(1), M(2) };
  M c[2] = { 5, 6 };
  M d[2][2];
}
struct H { M arr[2]; int tail; H(); ~H(); };
H::H() : tail(5) {}
H::~H() {}
