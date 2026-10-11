template <class F> int apply(F f, int x) { static int calls = 0; calls++; return f(x) + calls * 1000; }
static int local1() { struct F { int operator()(int a) const { return a * 2; } }; return apply(F(), 10); }
namespace { struct Impl { int v() const { return 100; } }; }
template <class T> int get(T *p) { static int n = 0; n++; return p->v() + n * 100; }
inline int helper(Impl *p) { return p->v(); }
static int sfn(int x) { struct L { int g() { return 50; } } l; return l.g() + x; }
struct { int z; int zz() { return z + 70; } } anon_obj = { 2 };
int tu2() { Impl i; return local1() + get(&i) + helper(&i) + sfn(1) + anon_obj.zz(); }
