// S3 probe: the standard library's use of member pointers and typeid:
// std::mem_fn, std::invoke on member pointers, std::function holding a member
// pointer and its target_type (typeinfo); shared_ptr waits for the atomic
// builtins.
#include <cstdio>
#include <functional>
#include <typeinfo>
struct P { int v = 5; int get(int x) const { return v + x; } int twice(int x) { return 2 * x; } };
int main() {
  P p; P *pp = &p;
  auto m = std::mem_fn(&P::get);
  std::printf("%d %d\n", m(p, 1), m(pp, 2));
  std::printf("%d %d\n", std::invoke(&P::twice, p, 4), std::invoke(&P::v, p));
  std::function<int(P &, int)> f = &P::twice;
  std::printf("%d\n", f(p, 7));
  std::function<int(const P &)> g = &P::v;
  std::printf("%d\n", g(p));
  std::printf("%d\n", typeid(f) == typeid(std::function<int(P &, int)>));
  return 0;
}
