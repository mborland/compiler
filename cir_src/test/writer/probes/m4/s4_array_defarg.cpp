// S4 probe: arrays of objects whose constructor has a default argument with a
// temporary (local, member, new[]), also with throwing constructors: the
// default argument is built for each element and destroyed after its call.
#include <cstdio>
int b, n;
struct A { A() { std::printf("A\n"); } A(const A &) { } ~A() { std::printf("~A\n"); if (b == n++) throw 1; } };
struct B { B(const A & = A()) { ++b; std::printf("B %d\n", b); if (b == 3) throw 2; } B(const B &); ~B() { --b; std::printf("~B\n"); } };
struct M { B arr[2]; M() { } };
void f() { b = 0; try { B bs[3]; } catch (int i) { std::printf("c %d b=%d\n", i, b); } }
void g() { b = 0; try { M m; } catch (int i) { std::printf("c %d b=%d\n", i, b); } }
void h() { b = 0; try { B *p = new B[4]; delete[] p; } catch (int i) { std::printf("c %d b=%d\n", i, b); } }
int main() { n = 100; f(); g(); h(); return 0; }
