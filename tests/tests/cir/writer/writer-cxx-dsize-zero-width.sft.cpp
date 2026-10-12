//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The data size (dsize) of a class ends at a zero-width bit-field's offset
// (S below has dsize 4), and a [[no_unique_address]] member declared through
// an alias counts as its data size, not its size.
extern "C" int printf(const char *, ...);
extern "C" void *memset(void *, int, unsigned long);
struct S { S(); char c; int : 0; };
S::S() : c(1) {}
struct T : S { char d; };
struct B { B() {} char b; };
using BA = B;
struct S2 { S2(); [[no_unique_address]] BA b; char d; };
S2::S2() : d(2) { b.b = 9; }
struct T2 : S2 { char e; };
int main_unused() {
  T t; memset(&t, 0, sizeof t); t.d = 5; S s; s.c = 7;
  *static_cast<S *>(&t) = s;
  printf("%d %d %d\n", (int)t.c, (int)t.d, (int)sizeof(S));
  T2 t2; t2.e = 6; S2 s2; s2.d = 3;
  *static_cast<S2 *>(&t2) = s2;
  printf("%d %d %d\n", (int)t2.d, (int)t2.e, (int)sizeof(S2));
  return 0;
}
