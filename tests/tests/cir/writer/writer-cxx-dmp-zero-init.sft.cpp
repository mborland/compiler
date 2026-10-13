//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The null value of a pointer to a data member is -1, not 0, so a type that
// contains one is not zero-initializable: zero-initialization (a
// namespace-scope definition without or with a partial initializer, a static
// local, S s{}, new S(), an array, a base, a member, the zero part of a
// constructor) uses INIT_ZERO on the typed glvalue or the unlisted parts of a
// partial aggregate constant, INIT_ZERO_BASE on a base subobject, and never
// ZERO_BYTES. A pointer to a member function is {0, 0} and a plain zero.
struct B { int a; int b; };
struct S { int x; int B::*p; };
struct W { S s; int y; };
struct D : S { int z; };
struct M { int B::*arr[2]; };
struct F { void (B::*f)(); int k; };
struct C { C(); S s; int k; };
struct E : D { E() = default; int q; };
S g;
S g2 = { 1, 0 };
W gw;
D gd;
M gm;
F gf;
int B::*gp;
int f1() { S s{}; return s.x; }
int f2() { S s = {1}; return s.x; }
int f3() { S *p = new S(); int r = p->x; delete p; return r; }
int f4() { S arr[2] = {}; return arr[0].x; }
int f5() { W w{}; return w.y; }
int f6() { S s; return s.x; }
int f7() { F f{}; return f.k; }
int f8() { static S ls; return ls.x; }
int f9() { D d{}; return d.z; }
int f10() { E e{}; return e.q; }
int f11() { C c; return c.k; }
int f12() { W *p = new W[3](); int r = p->y; delete[] p; return r; }
int f13() { M m{}; return m.arr[0] == nullptr; }
