// M4a probe: function-try-blocks: constructors (members and bases already
// constructed are destroyed before the handler; the handler rethrows), a
// destructor, a plain function, and a handler that returns (non-ctor).
#include <cstdio>
struct M { M(int x = 0) { std::printf("M(%d)\n", x); if (x == 7) throw 7; } ~M() { std::puts("~M"); } };
struct B { B() { std::puts("B"); } ~B() { std::puts("~B"); } };
struct C : B {
  M m1, m2;
  C(int x) try : B(), m1(1), m2(x) { std::puts("C body"); } catch (int i) { std::printf("C handler %d\n", i); }
  ~C() try { std::puts("~C body"); } catch (...) { std::puts("~C handler"); }
};
struct V { V() { std::puts("V"); } virtual ~V() {} };
struct D : virtual V { M m; D(int x) try : V(), m(x) { } catch (...) { std::puts("D handler"); } };
int f(int x) try { if (x) throw x; return 0; } catch (int i) { return i * 2; }
int main() {
  try { C c(7); } catch (int i) { std::printf("main %d\n", i); }
  { C c(0); }
  try { D d(7); } catch (int i) { std::printf("main D %d\n", i); }
  std::printf("%d %d\n", f(0), f(4));
  return 0;
}
