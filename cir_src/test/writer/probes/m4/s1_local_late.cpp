//options:--c++17
// M4 slice S1: a local polymorphic class whose constructor is defined after
// its function's region ended (std::make_unique<L> is instantiated later).
#include <memory>
#include <cstdio>
struct I { virtual int f() = 0; virtual ~I() {} };
auto make() {
  struct L : I { int f() override { return 3; } };
  return std::make_unique<L>();
}
int main() { auto p = make(); std::printf("%d\n", p->f()); return 0; }
