// M4a probe: size/nvsize/nvalign/dsize of dynamic classes, compared with
// clang -fdump-record-layouts.  Tail padding reuse and virtual bases.
#include <cstdio>
struct A { virtual void f() {} };
struct B : virtual A { };               // dsize 8, nvsize 8 (primary virtual)
struct C { virtual void f() {} char c; };           // tail padding
struct D : C { char d; };               // d placed in C's tail padding? (dsize)
struct V1 { char x[4]; };
struct L : virtual V1 { int i; };        // sizeof 32? nvsize 12
struct E { };
struct F : virtual E { int i; };         // empty virtual base
struct G { virtual void f() {} };
struct H : G, virtual E { char h; };
struct I : virtual G { };                // I's primary base is G (virtual)
struct J : virtual I { };                // primary: G (indirect virtual)
struct V2 : virtual V1 { int j; };       // V2 has its own virtual base
struct M : virtual V2 { int m; };        // V2 (with V1 inside) is a virtual base of M
struct N : virtual V1, virtual V2 { char n; };
struct O : N { char o; };                // tail padding of N (nvsize) reused? dsize of O
int main() {
  std::printf("%zu %zu %zu %zu %zu %zu %zu %zu %zu %zu\n", sizeof(B), sizeof(C),
    sizeof(D), sizeof(L), sizeof(F), sizeof(H), sizeof(I), sizeof(J), sizeof(G), sizeof(E));
  std::printf("%zu %zu %zu %zu\n", sizeof(V2), sizeof(M), sizeof(N), sizeof(O));
  return 0;
}
