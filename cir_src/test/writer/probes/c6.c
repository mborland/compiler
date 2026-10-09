int vla(int n) {
  int a[n];
  a[0] = n;
  int (*p)[n] = &a;
  return sizeof(a) + (*p)[0];
}
double _Complex cz(double _Complex z) { return z + 1; }
_Atomic int ai;
int gnu(int x) {
  int y = ({ int t = x; t + 1; });
  __asm__ volatile("nop");
  void *l = &&lab;
  goto *l;
lab:
  y += __builtin_expect(x, 1);
  y += __builtin_popcount(x);
  return y;
}
typedef int v4 __attribute__((vector_size(16)));
v4 vv;
