extern "C" int printf(const char *, ...);
int g;
static void fr(int x) { g += x; }
template <void (*F)(int)> struct Holder { static int run() { F(1); return 10; } };
template <template <class> class TT> struct Wrap { int v() const { return 1; } };
namespace { template <class T> struct Priv { T t; }; }
template <class T> int use_tt(Wrap<Priv> *w) { return w->v(); }
int tu2();
int tu1() { return Holder<&fr>::run() + g; }
int main() { printf("%d %d\n", tu1(), tu2()); return 0; }
