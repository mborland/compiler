// M4a probe: nvsize against data_size without virtual bases: displaced empty
// bases and members of an empty class type.
#include <cstdio>
struct E {};
struct E2 : E {};
struct A1 : E { E e; };                    // empty base E at 0, member e displaced
struct A2 : E, E2 { int i; };              // E2's base E collides: displaced
struct A3 { E e; int i; E f; };
struct A4 : A1 { char c; };                // derived from a class with a displaced base
struct A5 : E { virtual void f() {} E e; };
struct A6 : E, E2 {};                      // E2 displaced to 1: nvsize 2, no data
struct A7 : E, E2 { char c; };             // c at 0 shares the empty bases' bytes
int main() { std::printf("%zu %zu %zu %zu %zu\n", sizeof(A1), sizeof(A2), sizeof(A3), sizeof(A4), sizeof(A5));
  std::printf("%zu %zu\n", sizeof(A6), sizeof(A7));
  return 0; }
