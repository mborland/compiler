// M4a probe: throw in conditional regions (TERN arm, right operand of ||/&&),
// with temporaries in the operand and without, and a throw in a static init.
#include <cstdio>
struct A { int v; A(int x) : v(x) {} ~A() { std::printf("~A%d\n", v); } };
struct E { int v; E(const A &a) : v(a.v) {} };
int tern(int c) { return c ? throw E(A(1)) : 5; }
int tern2(int c) { return c ? 5 : throw 7; }
bool lor(bool b) { return b || (throw E(A(2)), false); }
bool land(bool b) { return b && (throw 3, true); }
int g = 0;
int sinit() { return g ? throw 9 : 4; }
int s = sinit();
int s2 = g ? throw 9 : 5;                    // a throw in a namespace-scope dynamic initializer
int loc() { static int l = g ? throw 8 : 6; return l; }   // and in a static local's
int main() {
  try { tern(1); } catch (E &e) { std::printf("E%d\n", e.v); }
  try { tern2(0); } catch (int i) { std::printf("i%d\n", i); }
  try { lor(false); } catch (E &e) { std::printf("E%d\n", e.v); }
  try { land(true); } catch (int i) { std::printf("i%d\n", i); }
  std::printf("%d %d %d %d\n", tern(0), tern2(1), (int)lor(true), (int)land(false));
  return (s - 4) + (s2 - 5) + (loc() - 6);
}
