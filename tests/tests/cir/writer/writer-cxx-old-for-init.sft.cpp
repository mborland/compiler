//type:fp
//options:--c++17 --old_for_init
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The old scope rule: a for-init variable is used after the loop.  A variable
// with a destructor, or a reference to such a class (it may extend a
// temporary), is diagnosed: the cleanup would end with the loop.
struct A { A(int); ~A(); int v; };
void use(A &);
void use(const A &);
int f() { for (A a(3); a.v < 5;) { use(a); } use(a); return 0; }
int g() { int s = 0; for (int i = 0; i < 3; i++) s += i; return i + s; }
int k() { for (const A &r = A(3); r.v < 5;) { use(r); } return 0; }
int m() { for (const int &r = 3; r < 5;) { } return 0; }
