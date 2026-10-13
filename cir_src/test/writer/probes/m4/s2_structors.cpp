//options:--c++17
// (the S2 writer test of the same name in tests/tests/cir/writer is this program)
// M4 slice S2: constructors and destructors of classes with virtual bases.  The
// constructor body is walked twice (C1, C2): a lambda in it is streamed once, a
// static local keeps one definition and has its STATIC_INIT in both bodies, a
// lifetime-extended temporary has one static object; a delegating constructor
// calls the target's C1 from C1 and the target's C2 with VTT_ADDR(self) from C2;
// the implicit copy constructor and assignment operator copy the virtual
// base; an indirect virtual base (G, with F : virtual W) is in G's own list.
extern "C" int printf(const char *, ...);
struct W {
  int w;
  W() : w(1) {}
  W(int x) : w(x) {}
  ~W() { printf("~W\n"); }
};
struct F : virtual W {
  int f;
  F() : W(2), f(3) {}
  F(int x) : W(x), f(x) {}
  ~F() { printf("~F\n"); }
};
struct G : F {
  int g;
  G() : g(4) {}
  G(int a, int b) : G() { g = a + b; }
  G(const G &) = default;
  G &operator=(const G &) = default;
};
struct Body : virtual W {
  int b;
  Body() : W(7), b(0) {
    static int calls = 0;
    calls++;
    auto add = [](int x) { return x + 1; };
    b = add(calls);
    const int &r = 5;
    b += r;
  }
  Body(int x) : Body() { b += x; }
};
struct Arr : virtual W {
  W held[2];
  Arr() { printf("Arr()\n"); }
};
int main() {
  G g1;
  G g2(1, 2);
  G g3(g2);
  g1 = g3;
  Body b(3);
  Arr a;
  Arr *p = new Arr;
  delete p;
  return g1.g + g1.w + b.b;
}
