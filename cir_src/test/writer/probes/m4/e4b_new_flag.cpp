// M4a probe: the lifetime of a new-expression's temporaries has
// inside_conditional_expression set also when the new is not conditional
// (an unconditional `new T(A(1), f)` and a conditional one).
#include <cstdio>
struct A { int v; A(int x) : v(x) {} ~A() { std::puts("~A"); } };
struct T { T(const A &, int f) { if (f) throw 1; } ~T() { std::puts("~T"); } };
T *mk(int f) { return new T(A(1), f); }
T *cond(bool c, int f) { return c ? new T(A(4), f) : nullptr; }
int main() {
  try { delete mk(0); } catch (int) {}
  try { mk(1); } catch (int) { std::puts("c1"); }
  try { cond(true, 1); } catch (int) { std::puts("c2"); }
  return 0;
}
