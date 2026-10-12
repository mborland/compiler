extern "C" int printf(const char *, ...);
int ca(); int cb(); int ia_a(); int ia_b();
int main() { int a1 = ca(); int b1 = cb(); int a2 = ca(); int ib = ia_b(); int ia = ia_a();
  printf("%d %d %d %d %d\n", a1, b1, a2, ib, ia); return 0; }
