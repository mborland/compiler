// M4a probe: structors of classes with virtual bases (C1/C2, D1/D2, VTT),
// the double walk (lambdas, static locals, default arguments), delegating
// constructors.
#include <cstdio>
struct V { V(int i) { std::printf("V(%d)\n", i); } virtual ~V() { std::puts("~V"); } int v; };
struct M { M() { std::puts("M"); } ~M() { std::puts("~M"); } };
struct A : virtual V { A() : V(1), m() { std::puts("A()"); } ~A() { std::puts("~A"); } M m; };
struct B : virtual V { B() : V(2) { std::puts("B()"); } virtual void f() {} };
struct D : A, B {
  M dm;
  D() : V(3), A(), B() { std::puts("D()"); static int s = 0; ++s; auto l = [this] { return dm; }; (void)l; }
  D(int) : D() { std::puts("D(int)"); }                  // delegating, has_vbases
  ~D() { std::puts("~D"); static int t = 0; ++t; }
};
struct E : D { E() : V(9) { std::puts("E()"); } };        // E constructs V
struct Lam : virtual V { int x; Lam() : V(0), x([]{ return 5; }()) { } };  // lambda in a mem-init
int main() { D d(1); E e; Lam l; return 0; }
