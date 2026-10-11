//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A { int i; };
A foo() { A a; a.i = 42; return a; }
A id(A x) { return x; }
A copy(const A &r) { return r; }
struct B { int j; int get() const { return j; } int set(int v) { j = v; return v; } static int sm(int x) { return x; } };
int use(B *p, B &r) { return p->get() + r.get() + B::sm(1) + p->set(2); }
struct D : A { int k; };
int up(D *d) { A *a = d; return a->i + d->i + d->k; }
D *down(A *a) { return (D*)a; }
int refs(int &x, const int &y) { int &z = x; z = y; return x; }
