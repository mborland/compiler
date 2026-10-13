//options: --c++14
// S5 probe: dynamic exception specifications (C++14): an allowed type passes,
// a violating one reaches std::unexpected, whose handler throws an allowed
// type or std::bad_exception; throw() on a function that throws; a base class
// in the list allows a derived one; noexcept(false) is not a dynamic spec.
#include <cstdio>
#include <exception>
struct Base { }; struct Der : Base { }; struct Other { };
void thrower(int k) { if (k == 1) throw 1; if (k == 2) throw Der(); if (k == 3) throw Other(); if (k == 4) throw 2.5; }
void a(int k) throw(int, Base) { thrower(k); }
void b(int k) throw(std::bad_exception) { thrower(k); }
void c(int k) throw() { (void)k; }
void handler() { std::puts("unexpected"); throw 7; }
int main() {
  std::set_unexpected(handler);
  for (int k = 0; k < 4; ++k) {
    try { a(k); std::puts("a ok"); } catch (int i) { std::printf("int %d\n", i); } catch (Base &) { std::puts("Base"); } catch (...) { std::puts("other"); }
  }
  try { a(4); } catch (int i) { std::printf("a(4) int %d\n", i); } catch (...) { std::puts("a(4) other"); }
  try { c(1); std::puts("c ok"); } catch (...) { std::puts("c other"); }
  return 0;
}
