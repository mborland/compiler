extern "C" int printf(const char *, ...);
struct Empty { };
struct A { A() : a(true) { } bool a; ~A() { if (!a) printf("BAD a\n"); } };
struct B : Empty { B() : Empty() { } ~B() { } };
struct C : A, B { C() : A(), B() { } ~C() { } };
struct B5 : Empty { B5() : Empty() { } };
struct C5 : A, B5 { C5() : A(), B5() { } };
bool f(bool &b) { b = true; return false; }
struct L {
  int a, z;
  L() : a(4), z(0) {}
  L(const L &x) : a(x.a), z(1) {}
  int run() {
    auto c = [*this] { return a + z; };
    auto d = [this] { return a + z; };
    return c() * 10 + d();
  }
};
int main() {
  C c; C5 c5;
  printf("%d %d\n", c.a, c5.a);
  bool b = false; b |= f(b); printf("b %d\n", b);
  L l; printf("%d\n", l.run());
  return 0;
}
