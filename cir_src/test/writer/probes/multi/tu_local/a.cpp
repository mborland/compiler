extern "C" int printf(const char *, ...);
template <class F> int apply(F f, int x) { static int calls = 0; calls++; return f(x) + calls * 1000; }
static int local1() { struct F { int operator()(int a) const { return a + 1; } }; return apply(F(), 10); }
namespace { struct Impl { int v() const { return 1; } }; }
template <class T> int get(T *p) { static int n = 0; n++; return p->v() + n * 100; }
inline int helper(Impl *p) { return p->v(); }
static int sfn(int x) { struct L { int g() { return 5; } } l; return l.g() + x; }
struct { int z; int zz() { return z + 7; } } anon_obj = { 1 };
int tu2();
int tu1() { Impl i; return local1() + get(&i) + helper(&i) + sfn(1) + anon_obj.zz(); }
int main() { printf("%d %d\n", tu1(), tu2()); return 0; }
