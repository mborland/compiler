extern "C" int printf(const char *, ...);
struct NC { int x; int y; NC(const NC &) = delete; int sum() const { return x + y; } };
NC mk(int v) { return NC{v, v + 1}; }
int take(NC n) { return n.x * 10 + n.y; }
NC nrvo(int v) { if (v > 2) return mk(v * 3); return mk(v); }
int viatemp(int v) { return mk(v).sum() + take(mk(v + 1)); }
namespace ns {
  template <class T> struct Box { T v; T get() const { return v; } static int count; };
  template <class T> int Box<T>::count = 3;
  inline int twice(int x) { return 2 * x; }
}
namespace { int hidden(int x) { return x + 100; } struct Priv { int p; int get() const { return p; } }; }
struct E {}; struct D1 : E { int x; int g() const { return x; } };
struct B1 { int a; char c; };
struct D3 : B1 { char d; };
int dsz(B1 *p, B1 *q) { *p = *q; return p->a; }
const D1 &pick(const D1 &a, const D1 &b, bool c) { return c ? a : b; }
enum class Col : short { r = 1, g = 2 };
int col(Col c) { return c == Col::g ? 7 : 8; }
int sl() { static int k = 5; k += 1; return k; }
struct Counter { static int n; static int next() { return ++n; } };
int Counter::n = 0;
int main() {
  ns::Box<int> b{4};
  NC n = nrvo(3);
  D1 d1; d1.x = 9; D1 d2; d2.x = 8;
  D3 d3; d3.a = 1; d3.c = 2; d3.d = 3; B1 b1; b1.a = 77; b1.c = 5;
  dsz(&d3, &b1);
  Priv pr{6};
  printf("%d %d %d %d\n", viatemp(2), n.x + n.y, take(mk(4)), b.get() + ns::Box<int>::count);
  printf("%d %d %d %d\n", ns::twice(4), hidden(1), pr.get(), pick(d1, d2, false).g());
  printf("%d %d %d %d %d\n", d3.a, (int)d3.c, (int)d3.d, col(Col::g), sl() + sl());
  int c1 = Counter::next(); int c2 = Counter::next(); printf("%d %d\n", c1, c2);
  return 0;
}
