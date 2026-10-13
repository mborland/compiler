// M4a probe: throw expressions: with temporaries in the initializer, a
// throwing initializer (free exception), throw in a conditional arm and in a
// logical operand, rethrow, throw of a class with a copy constructor.
#include <cstdio>
struct A { int v; A(int x) : v(x) { std::printf("A(%d)\n", x); } ~A() { std::printf("~A(%d)\n", v); } };
struct E { A a; int w; E(A x, int y) : a(x), w(y) { if (y < 0) throw 5; } E(const E &o) : a(o.a), w(o.w) { std::puts("E copy"); } };
int mk(int x) { if (x == 2) throw 'c'; return x; }
int tern(bool b) { return b ? throw 1 : 7; }
bool lor(bool b) { return b || (throw 2, false); }
int main() {
  try { throw E(A(1), 2); } catch (E &e) { std::printf("caught E %d\n", e.w); }
  try { int k = mk(2); throw E(A(3), k); } catch (char c) { std::printf("char %c\n", c); }
  try { throw E(A(4), -1); } catch (int i) { std::printf("int %d\n", i); }
  try { tern(true); } catch (int i) { std::printf("tern %d\n", i); }
  try { lor(false); } catch (int i) { std::printf("lor %d\n", i); }
  std::printf("%d %d\n", tern(false), lor(true));
  return 0;
}
