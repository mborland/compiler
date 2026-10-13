//type:fp
//options:--c++20 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// W4: namespace-scope arrays of objects with constructors and destructors
struct A { A(); ~A(); int v; };
A one[1];
A grid[2][2];
namespace N { A pair[2]; }
template <class T> struct S { S() { } int i; };
static S<int> sa[2];
// W7: a record whose by-value member points back to the class that derives
template <class T> struct W { T *p; W() : p(0) { } };
template <class T> struct E { W<T> w; };
struct B : E<B> { int x; };
B b;
// W9: a class conditional that initializes a static
struct C2 { C2(int); ~C2(); int v; };
int sel;
static C2 chosen = sel ? C2(7) : C2(8);
// __builtin_addressof
struct O { int v; int *operator&(); };
O *addr(O &o) { return __builtin_addressof(o); }
// names of the kinds that are not supported
struct P { int m; int f(); };
int stmt_expr() { return ({ int x = 1; x; }); }
void asm_use() { asm("nop"); }
void *pm_addr(P *p, int (P::*f)()) { return (void *)(p->*f); }
void *pm_addr2(P &p, int (P::*f)()) { return (void *)(p.*f); }
