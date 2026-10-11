//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct M { int v; M(); M(int); M(const M &); ~M(); };
struct B { int b; B(int x) : b(x) {} ~B() {} };
struct D : B { M m; int k = 7; int arr[2]; D(); D(int x); ~D(); };
D::D() : B(1) {}
D::D(int x) : D() { k = x; }
D::~D() {}
struct P { int x, y; };
struct Q { P p; M m; Q() : p(), m(4) {} };
struct Z : B { int z; Z() : B(2), z() {} };
inline void use_inline(M &m) { M copy(m); }
int pointer(M *a, D *d) { return a->v + d->k; }
void call_inline(M &m) { use_inline(m); }
