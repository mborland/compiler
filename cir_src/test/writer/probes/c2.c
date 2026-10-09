#include <stdarg.h>
struct B { unsigned x:3; int y:5; char c; };
struct An { int k; union { int i; float f; }; struct { short a, b; }; };
int sum(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int t = 0;
  for (int i = 0; i < n; i++) t += va_arg(ap, int);
  va_end(ap);
  return t;
}
void m(struct B *pb, struct An *pa, long l, float f, int *p, int i, unsigned u) {
  pb->x = 5;
  pb->y += 2;
  pb->x++;
  l += f;
  l <<= i;
  u >>= 2;
  p++;
  --p;
  p += i;
  p -= 2;
  i = pa->i + pa->a;
  i = u < i;
  i = p == 0;
  i = (p != (int*)0);
  i = f > 1.5;
  i = i / 3 % 2;
  l = u;
  u = -1;
}
