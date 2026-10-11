//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct Empty { };
struct A { A(); A(const A &); ~A(); bool a; };
struct B : Empty { B() : Empty() { } };
struct C : A, B { C() : A(), B() { } };
int copy_capture() { A x; auto y = [x]() mutable { return x; }; return y().a; }
struct L { int a; L(); L(const L &); int run() { auto c = [*this] { return a; }; return c(); } };
int r;
int f();
const int *p;
void static_local_ref() { static const int &i = f(); if (p && p != &i) ++r; p = &i; }
bool g(bool &b);
void compound_order() { bool b = false; b |= g(b); }
struct Pad { char c; int i; };
struct Wrap { Pad a; Wrap() : a() { } };
Wrap w;
Pad zp = Pad();
A make() { int cnt = 3; lab: A a; if (cnt--) goto lab; return a; }
struct V { virtual int h(); };
struct W : virtual V { };
W we;
W *pw = &we;
int use_failed() { return (int)sizeof(W) + (we.h(), 0); }
struct N { static void *operator new(unsigned long); static void operator delete(void *); N(); int v; };
N *make_n() { return new N; }
void del_n(N *n) { delete n; }
