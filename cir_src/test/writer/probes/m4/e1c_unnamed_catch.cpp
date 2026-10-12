// M4a probe: an unnamed by-value catch parameter is still copy-constructed
// and destroyed (g++ and clang agree).
#include <cstdio>
struct B { B() {} B(const B &) { std::puts("B copy"); } ~B() { std::puts("~B"); } };
int main() {
  try { throw B(); } catch (B) { std::puts("unnamed by value"); }
  try { throw B(); } catch (B b) { std::puts("named by value"); }
  return 0;
}
