//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
static int g;
static void fr(int x) { g += x; }
template <void (*F)(int)> struct Holder { static int run() { F(1); return 10; } };
template <template <class> class TT> struct Wrap { int v() const { return 1; } };
namespace { template <class T> struct Priv { T t; }; }
int tu1() { return Holder<&fr>::run() + g; }
int tt(Wrap<Priv> *w) { return w->v(); }
