//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
namespace { struct Impl { int v() const { return 1; } static int sv; int m; }; int Impl::sv = 3; }
template <class T> int get(T *p) { static int n = 0; n++; return p->v() + n; }
template <class T> int ident(T *p) { static int k = 0; return ++k + (p != 0); }
template <class F> int apply(F f, int x) { static int calls = 0; calls++; return f(x) + calls; }
inline int helper(Impl *p) { return p->v(); }
int use(Impl *p) { return p->v() + get(p) + helper(p) + Impl::sv; }
struct { int z; int zz() { return z + 7; } } anon_obj = { 1 };
int use_anon() { return anon_obj.zz() + ident(&anon_obj); }
static int sfn(int x) { struct L { int g() { return 5; } } l; return l.g() + x; }
static int local1() { struct F { int operator()(int a) const { return a + 1; } }; return apply(F(), 10); }
int external_user() { return sfn(1) + local1(); }
namespace { template <class T> struct W { int w() { return 1; } }; }
int use_w(W<int> *p) { return p->w(); }
typedef struct { int q; int get() const { return q; } } Named;
int use_named(Named *n) { return n->get(); }
