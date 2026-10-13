// Host of cxx-s4-eh-return-value.test: a function that returns a class by
// value while a local's destructor throws.  Prints what ran.
#include <cstdio>
struct R { int v; R(int); ~R(); };
struct Td { int v; Td(int); ~Td() noexcept(false); };
R::R(int f) : v(f) { std::printf("R ctor %d\n", v); }
R::~R() { std::printf("R dtor %d\n", v); }
Td::Td(int f) : v(f) { std::printf("Td ctor %d\n", v); }
Td::~Td() noexcept(false) {
  std::printf("Td dtor %d\n", v);
  if (v == 7) throw 77;
}
extern "C" R c_f(int);
extern "C" R c_g(int);
template <class F> static void run(const char *name, F f, int k) {
  std::printf("%s(%d)\n", name, k);
  try {
    R x = f(k);
    std::printf("returned %d\n", x.v);
  } catch (int i) {
    std::printf("caught %d\n", i);
  }
}
int main() {
  run("c_f", c_f, 3);
  run("c_f", c_f, 7);
  run("c_g", c_g, 3);
  run("c_g", c_g, 7);
  return 0;
}
