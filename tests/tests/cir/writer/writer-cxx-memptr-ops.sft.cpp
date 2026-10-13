//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Member pointer operations: .* and ->* on data members (PM_FIELD, a glvalue)
// and calls (PM_CALL: the object first, then the method pointer, then the
// arguments), the conversions between pointers to members of a base and of a
// derived class (PM_CAST to_derived and to_base, one direct step), comparison
// and the conversion to bool.
struct B1 { int b1; virtual int vf(int x); };
struct B2 { int b2; int g(int x); };
struct D : B1, B2 { int d; };
typedef int (D::*DM)(int);
typedef int D::*DD;
int f1(D &o, DD p) { return o.*p; }
int f2(D *o, DD p) { return o->*p; }
void f3(D &o, DD p) { o.*p = 3; (o.*p)++; }
int f4(D &o, DM m, int a) { return (o.*m)(a); }
int f5(D *o, DM m, int a) { return (o->*m)(a); }
D make();
int f6(DD p) { return make().*p; }
int f7(int B2::*p) { DD q = p; return q == nullptr; }
int B2::*f8(DD p) { return static_cast<int B2::*>(p); }
DM f9(int (B2::*m)(int)) { return m; }
int (B2::*f10(DM m))(int) { return static_cast<int (B2::*)(int)>(m); }
bool f11(DD p, DM m) { return p && m; }
int f12(DD p, DD q, DM m, DM n) { return (p == q) + (p != q) + (m == n) + (m != n); }
int f13(D &o, DM m) { return m ? (o.*m)(1) : 0; }
