extern "C" int printf(const char *, ...);
struct B { int x; constexpr B(int v) : x(v) {} virtual int f() const { return x; } };
struct C { int c; };
struct D : C, B { int y; constexpr D() : C{3}, B(7), y(9) {} int f() const override { return B::f() + y; } };
struct M { B b; int m; constexpr M() : b(2), m(5) {} };
extern const D gd;
extern const M gm;
int use();
