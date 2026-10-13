// S5 probe: function-try-block edge cases: constructors of classes with
// virtual bases (C1 and C2 are walked twice), delegating constructors, a
// member that throws, a handler that translates the exception, destructors, a
// plain function, a template, the exception reaching the caller after the
// implicit rethrow.
#include <cstdio>
struct T { T(int v) { std::printf("T(%d)\n", v); if (v < 0) throw v; } ~T() { std::puts("~T"); } };
struct V { V() { std::puts("V"); } ~V() { std::puts("~V"); } };
struct B : virtual V { T t; B(int v) try : t(v) { std::puts("B body"); } catch (int e) { std::printf("B catch %d\n", e); } };
struct D : B { T u; D(int a, int b) try : B(a), u(b) { std::puts("D body"); } catch (int e) { std::printf("D catch %d\n", e); } };
struct Dl { T m; Dl(int v) : m(v) { } Dl() try : Dl(-4) { } catch (int e) { std::printf("Dl catch %d\n", e); } };
struct Ds { Ds() { } ~Ds() try { std::puts("Ds body"); } catch (...) { std::puts("Ds catch"); } };
int plain(int x) try { if (x) throw x; return 0; } catch (int e) { return e + 100; }
template <class U> struct Tp { U u; Tp(int v) try : u(v) { } catch (...) { std::puts("Tp catch"); } };
int main() {
  try { B b(1); } catch (...) { std::puts("no"); }
  try { B b(-1); } catch (int e) { std::printf("main %d\n", e); }
  try { D d(1, -2); } catch (int e) { std::printf("main %d\n", e); }
  try { D d(-3, 1); } catch (int e) { std::printf("main %d\n", e); }
  try { Dl d; } catch (int e) { std::printf("main %d\n", e); }
  { Ds d; }
  std::printf("%d %d\n", plain(0), plain(5));
  try { Tp<T> t(-6); } catch (int e) { std::printf("main %d\n", e); }
  return 0;
}
