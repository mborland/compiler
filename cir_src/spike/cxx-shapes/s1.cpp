#include <cstdio>
struct A {
  int x;
  A() : x(1) { std::printf("A() %p\n", (void*)this); }
  A(const A &o) : x(o.x) { std::printf("A(const A&) %p <- %p\n", (void*)this, (const void*)&o); }
  ~A() { std::printf("~A() %p\n", (void*)this); }
};
A make() { return A(); }
A chain() { return make(); }
A nrvo() { A a; a.x = 7; return a; }
void take(A p) { std::printf("take param %p x=%d\n", (void*)&p, p.x); }
int main() {
  std::printf("-- A a = make()\n");
  { A a = make(); std::printf("a at %p\n", (void*)&a); }
  std::printf("-- chain\n");
  { A a = chain(); std::printf("a at %p\n", (void*)&a); }
  std::printf("-- nrvo\n");
  { A a = nrvo(); std::printf("a at %p x=%d\n", (void*)&a, a.x); }
  std::printf("-- take(A())\n");
  take(A());
  std::printf("-- take(make())\n");
  take(make());
  return 0;
}
