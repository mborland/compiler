//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A partly constant aggregate initializer of a variable of static storage
// duration: VAR_INIT has the constant elements (zero for the others) and the
// STATIC_INIT (PARTIAL) only stores the others, without zero-filling; the
// same for a function-local static (guarded).  gnu/cpp/cpp0x/constexpr-static4.
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
int main_unused() {
  printf("%d %d\n", ar[0], ar[1]);
  printf("%d %d %d\n", gp.a, gp.b, gp.c);
  printf("%d\n", h(10));
  printf("%d\n", h(20));
  return 0;
}
