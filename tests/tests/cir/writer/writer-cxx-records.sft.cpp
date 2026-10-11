//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct E {};
struct B1 { int a; char c; };
struct D1 : B1 { char d; };
struct D2 : E { int x; };
struct M { char m; };
struct D3 : B1, M { int z; };
struct NU { [[no_unique_address]] E e; int q; };
struct P { virtual void f(); int v; };
struct DP : P { int w; };
struct R { int &r; const int &cr; int *p; };
struct Dt { ~Dt(); int i; };
struct Cp { Cp(const Cp&); int i; };
struct Tp { int i; };
int sink(B1*, D1*, D2*, D3*, NU*, P*, DP*, R*, Dt*, Cp*, Tp*);
int use(B1 *b, D1 *d1, D2 *d2, D3 *d3, NU *n, P *p, DP *dp, R *r, Dt *dt, Cp *cp, Tp *tp) { return sink(b, d1, d2, d3, n, p, dp, r, dt, cp, tp); }
