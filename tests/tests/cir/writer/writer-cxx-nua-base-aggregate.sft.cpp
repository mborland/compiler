//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Aggregates with base classes: the entries are indexed by the record's
// fields (bases first), also with an empty [[no_unique_address]] member, and
// the typed constants take the types of their members.
extern "C" int printf(const char *, ...);
struct E {};
struct B { int b; };
struct EB {};
struct D : B { int n; [[no_unique_address]] E e; };
struct D3 : EB { int n; [[no_unique_address]] E e; int k; };
struct D4 : B { int a; int c; [[no_unique_address]] E e; };
struct D2 : B { char c; long m; short s; };
int f() { return 0x12345678; }
D gd{ { 1 }, f(), {} };
D3 gd3{ {}, f(), {}, 5 };
D4 gd4{ { 2 }, f(), 6, {} };
D2 gd2{ { f() }, 'x', 99L, 5 };
int main() {
  D ld{ { 3 }, f(), {} };
  D3 ld3{ {}, f(), {}, 7 };
  D4 ld4{ { 4 }, f(), 8, {} };
  D2 ld2{ { f() }, 'y', 77L, 6 };
  printf("%d %x %d %x %d %x %d %d %x %d\n", gd.b, gd.n, ld.b, ld.n, gd3.k,
         gd3.n, ld3.k, gd4.c, gd4.a, ld4.c);
  printf("%x %c %ld %d %x %c %ld %d\n", gd2.b, gd2.c, gd2.m, gd2.s, ld2.b,
         ld2.c, ld2.m, ld2.s);
  return 0;
}
