//type:fp
//options:--c++20
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Aggregate initialization with an empty [[no_unique_address]] member: it
// writes nothing (the member shares the address of the one before it).
extern "C" int printf(const char *, ...);
struct E {};
struct Z { Z() {} };
struct M { int n; [[no_unique_address]] E e; };
struct M2 { int n; [[no_unique_address]] Z z; int k; };
struct M3 { int a; [[no_unique_address]] E e; [[no_unique_address]] E e2; };
int f() { return 0x12345678; }
M gm{ f(), {} };
M2 gm2{ f(), Z(), 7 };
M cm{ 0x1234, {} };
int main() {
  M lm{ f(), {} };
  M2 lm2{ f(), Z(), 9 };
  M3 lm3{ f(), {}, {} };
  static M sm{ f(), {} };
  printf("%x %x %x %x\n", gm.n, lm.n, sm.n, cm.n);
  printf("%x %d %x %d %x\n", gm2.n, gm2.k, lm2.n, lm2.k, lm3.a);
  return 0;
}
