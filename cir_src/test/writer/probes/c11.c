struct S { int x; };
int *pick(int c, int *p, long l, float f, struct S a, struct S b, unsigned u) {
  double d = c ? 1 : 2.0;
  long m = c ? l : c;
  struct S s = c ? a : b;
  int *q = c ? p : 0;
  l = u < c;
  l = f * l + u;
  u = c << l;
  return q;
}
