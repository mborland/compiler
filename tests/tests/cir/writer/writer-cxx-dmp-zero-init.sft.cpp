//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The null value of a pointer to a data member is -1, not 0, and M3 sends the
// type as opaque storage: any zero-initialization of an object that contains
// one (a namespace-scope definition without or with a partial initializer, a
// static local, S s{}, new S(), an array or base that contains one) is
// unsupported until M4, instead of silently writing 0. A pointer to a member
// function is {0, 0} and is fine.
struct B { int a; int b; };
struct S { int x; int B::*p; };
struct W { S s; int y; };
struct D : S { int z; };
struct M { int B::*arr[2]; };
struct F { void (B::*f)(); int k; };
S g;
S g2 = { 1, 0 };
W gw;
D gd;
M gm;
F gf;
extern S ex;
int B::*gp;
int f1() { S s{}; return s.x; }
int f2() { S s = {1}; return s.x; }
int f3() { S *p = new S(); int r = p->x; delete p; return r; }
int f4() { S arr[2] = {}; return arr[0].x; }
int f5() { W w{}; return w.y; }
int f6() { S s; return s.x; }
int f7() { F f{}; return f.k; }
int f8() { static S ls; return ls.x; }
int f9() { return gf.k + ex.x; }
