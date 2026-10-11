#include <cstdio>
struct A {
  int x;
  A() : x(1) { std::printf("A() %p\n", (void*)this); }
  A(int v) : x(v) { std::printf("A(int) %p\n", (void*)this); }
  A(const A &o) : x(o.x) { std::printf("A(const A&) %p <- %p\n", (void*)this, (const void*)&o); }
  ~A() { std::printf("~A() %p\n", (void*)this); }
};
struct B { A a; int y; };
A make() { return A(5); }
B makeb() { return B{A(2), 3}; }
A pick(bool c) { A a(10), b(20); return c ? a : b; }
void take(A p) { std::printf("take %p x=%d\n", (void*)&p, p.x); }
void takeref(const A &r) { std::printf("takeref %p x=%d\n", (const void*)&r, r.x); }
int main() {
  std::printf("-- B b{make(), 3}\n");
  { B b{make(), 3}; std::printf("b.a at %p\n", (void*)&b.a); }
  std::printf("-- makeb\n");
  { B b = makeb(); std::printf("b.a at %p\n", (void*)&b.a); }
  std::printf("-- discard make()\n");
  make();
  std::printf("-- const A &r = make()\n");
  { const A &r = make(); std::printf("r at %p\n", (const void*)&r); }
  std::printf("-- pick(true)\n");
  { A a = pick(true); std::printf("a at %p x=%d\n", (void*)&a, a.x); }
  std::printf("-- takeref(make())\n");
  takeref(make());
  std::printf("-- take(A a) of lvalue copy\n");
  { A a(3); take(a); }
  return 0;
}
