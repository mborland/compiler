// The C++ the hand-written ECIL stream test/cirgen/cxx-copy-elision.test
// encodes; used with scripts/cir-compare.py --cxx against clang.
struct A { int v; A(int x); A(const A &s); };
extern void *g_this, *g_param;
extern int g_copies;
A::A(int x) { g_this = this; v = x; }
A::A(const A &s) { ++g_copies; v = s.v; }
A make(int x) { return A(x); }
A chain(int x) { return make(x); }
A nrvo(int x) { A a(x); return a; }
int take(A a) { g_param = &a; return a.v; }
