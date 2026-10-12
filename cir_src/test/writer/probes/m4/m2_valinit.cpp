// M4a probe: value- and zero-initialization of classes with data-member
// pointers (null is -1).  Does EDG ever give a bare zero-fill here?
#include <cstdio>
#include <new>
struct S { long a; long S::*pm; long b; };
struct T : S { int t; };
struct U { S s; S arr[2]; };
struct ND { long a; long ND::*pm; ND() {} };
struct NS { ND nd; long z; };
S gs;                                 // static zero-init
S gs2{};                              // value-init
S garr[3];
U gu;
T gt{};
void show(const S &s) { std::printf("%ld %ld %ld\n", s.a, s.pm ? 1L : 0L, s.b); }
S *dyn_paren(int n) { return new S[n](); }
S *dyn_brace(int n) { return new S[n]{}; }
S *dyn_brace1(int n) { return new S[n]{S{1, nullptr, 2}}; }
ND *dyn_nd(int n) { return new ND[n](); }
ND *one_nd() { return new ND(); }
int main() {
  S l{}; S l2 = {}; S l3 = S(); S l4 = {1};
  S *np = new S(); S *np2 = new S{}; S *na = new S[2](); S *nb = new S[2]{};
  alignas(S) char buf[sizeof(S)]; S *pl = new (buf) S();
  static S ls{};
  T t{}; U u{}; NS ns{}; U *nu = new U();
  show(l); show(l2); show(l3); show(l4); show(*np); show(*np2); show(na[1]); show(nb[1]);
  show(*pl); show(ls); show(t); show(gs); show(gs2); show(garr[2]); show(gu.arr[1]); show(gt);
  show(u.arr[0]); show(nu->s); std::printf("%d\n", ns.nd.pm == nullptr);
  delete[] dyn_paren(2); delete[] dyn_brace(2); delete[] dyn_brace1(2); delete[] dyn_nd(2); delete one_nd();
  return 0;
}
