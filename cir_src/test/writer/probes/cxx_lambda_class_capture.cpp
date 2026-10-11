extern "C" int printf(const char *, ...);
struct A {
  int i;
  A(): i(42) {}
  A(const A&) = default;
  A(A&& a): i(a.i) { a.i = 0; }
};
int main() {
  A x;
  auto y = [x] () mutable { x.i++; return x; };
  printf("%d\n", y().i);
  printf("%d\n", y().i);
  return 0;
}
