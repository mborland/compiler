//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct Base { int v; };
struct U1 { int a; union { int b; double c; union { short e; long f; }; }; int d; };
U1 u1 = { 1, 2, 3 };
int anon_access(U1 &x, U1 *p) { x.b = 3; p->f = 4; return x.a + p->e + (int)x.c; }
struct T1 { int q; struct { int u; int v; }; };
int anon_struct(T1 t) { return t.v + T1().u; }
static union { short sx; };
int static_anon() { return sx; }
struct C0 : Base { int j; };
struct A0 {};
struct B0 : A0 {};
struct C1 : A0, B0 { int j; };
C1 c1;
struct B2 { int a; char c; };
struct D2 : B2 { B2 inner; };
struct E0 {};
struct E1 : E0 { int x; ~E1() {} };
struct N0 { N0() {} int a; char c; };
struct N1 : N0 { char d; };
D2 d2; E1 e1; N1 n1; C0 c0;
struct M { union { int m_1 = 0; int m_2; }; ~M(); };
M m;
