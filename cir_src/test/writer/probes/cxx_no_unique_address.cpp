//options:--c++20
// Value-initialization of an empty [[no_unique_address]] member must not
// write the byte at its address (it overlaps an earlier member).
extern "C" int printf(const char *, ...);
struct E {};
struct M { int n; [[no_unique_address]] E e; M() : n(42), e() {} };
struct M2 { int n; [[no_unique_address]] E e = E(); M2() : n(7) {} };
struct M3 { int n; [[no_unique_address]] E e{}; int k; M3() : n(1), k(2) {} };
struct Z { Z() {} };
struct M4 { int n; [[no_unique_address]] Z z; M4() : n(5), z() {} };
int main() {
  M m;
  M2 m2;
  M3 m3;
  M4 m4;
  printf("%d %d %d %d %d\n", m.n, m2.n, m3.n, m3.k, m4.n);
  return 0;
}
