// M4a probe: member pointer conversions through several levels, and
// pointers to members of a base reached through the derived class.
#include <cstdio>
struct A { int a; int fa(int x) { return x + 1; } virtual int va(int x) { return x + 2; } };
struct B : A { int b; };
struct C : B { int c; int fc(int x) { return x + 3; } };
struct P { int p; int fp(int x) { return x + 4; } };
struct M : P, C { };                      // C at non-zero offset
int main() {
  int A::*pa = &A::a;
  int C::*pc = pa;                        // two levels: A -> B -> C
  int (A::*fa)(int) = &A::fa;
  int (C::*fc)(int) = fa;
  int (A::*va)(int) = &A::va;
  int (C::*vc)(int) = va;
  int M::*pm = pc;                        // C -> M: offset of C in M
  int (M::*fm)(int) = fc;
  M m; m.a = 1; m.c = 2;
  C &c = m;
  std::printf("%d %d %d %d %d %d\n", c.*pc, m.*pm, (c.*fc)(1), (m.*fm)(1), (c.*vc)(1), (m.*static_cast<int (M::*)(int)>(vc))(1));
  int C::*back = static_cast<int C::*>(pa);
  int A::*pa2 = static_cast<int A::*>(pc);
  std::printf("%d %d\n", c.*back, m.*pa2);
  return 0;
}
