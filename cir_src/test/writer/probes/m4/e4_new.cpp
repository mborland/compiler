// M4a probe: EH dynamic initializations: new with a throwing constructor
// (free storage), with temporaries (T(A())), arrays new U[n]{...},
// conditional new, delete expression, array new/delete[], aggregate
// initialization of a local array with throwing elements (partial
// destruction), member arrays.
#include <cstdio>
#include <cstddef>
#include <new>
struct A { int v; A(int x) : v(x) { std::printf("A(%d)\n", x); } ~A() { std::printf("~A(%d)\n", v); } };
struct T { T(const A &, int f) { if (f) throw 1; } ~T() { std::puts("~T"); } };
struct U { U() { std::puts("U()"); } U(int x) { std::printf("U(%d)\n", x); if (x == 9) throw 2; } U(const A &, int f) { if (f) throw 3; } ~U() { std::puts("~U"); } };
struct Agg { A a; A b; };
struct Mem { U arr[3]; Mem() {} };
struct PL { PL(int f) { if (f) throw 4; } static void *operator new(std::size_t n, int) { std::puts("PL new"); return ::operator new(n); }
            static void operator delete(void *p, int) { std::puts("PL placement delete"); ::operator delete(p); }
            static void operator delete(void *p) { std::puts("PL delete"); ::operator delete(p); } };
T *mk(int f) { return new T(A(1), f); }
U *mkarr(int f) { return new U[3]{U(1), U(f ? 9 : 2), U()}; }
T *cond(bool c, int f) { return c ? new T(A(4), f) : nullptr; }
int main() {
  try { delete mk(0); } catch (int) {}
  try { mk(1); } catch (int i) { std::printf("c %d\n", i); }
  try { delete[] mkarr(0); } catch (int) {}
  try { mkarr(1); } catch (int i) { std::printf("c %d\n", i); }
  try { delete cond(true, 0); cond(true, 1); } catch (int i) { std::printf("c %d\n", i); }
  try { U loc[4] = {U(1), U(2), U(9), U(4)}; } catch (int i) { std::printf("c %d\n", i); }
  try { Agg g{A(1), A(2)}; } catch (int i) { std::printf("c %d\n", i); }
  try { U *p = new U[2](); delete[] p; } catch (int i) { std::printf("c %d\n", i); }
  try { Mem m; } catch (int i) { std::printf("c %d\n", i); }
  try { new (7) PL(1); } catch (int i) { std::printf("c %d\n", i); }
  try { PL *p = new (7) PL(0); delete p; } catch (int i) { std::printf("c %d\n", i); }
  return 0;
}
