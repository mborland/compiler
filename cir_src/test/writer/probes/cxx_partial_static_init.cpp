//options:--c++17
// An aggregate that is partly constant: the constant elements are static
// initialization (VAR_INIT), the others are built by a STATIC_INIT with the
// PARTIAL flag, at namespace scope and for a function-local static
// (gnu/cpp/cpp0x/constexpr-static4).  Same output as g++.
extern "C" int printf(const char *, ...);
extern int ar[2];
int f() { printf("f: %d %d\n", ar[0], ar[1]); return 7; }
int i = f();
int ar[2] = { 42, i };
struct P { int a; int b; int c; };
extern P gp;
int g() { printf("g: %d %d %d\n", gp.a, gp.b, gp.c); return 9; }
int j = g();
P gp = { 1, j, 3 };
int h(int k) {
  static int sl[3] = { 5, k, 6 };
  static P sp = { k, 2, k + 1 };
  return sl[0] + sl[1] + sl[2] + sp.a + sp.b + sp.c;
}
int main() {
  printf("%d %d\n", ar[0], ar[1]);
  printf("%d %d %d\n", gp.a, gp.b, gp.c);
  printf("%d\n", h(10));
  printf("%d\n", h(20));
  return 0;
}
