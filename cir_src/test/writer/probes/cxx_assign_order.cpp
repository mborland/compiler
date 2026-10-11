//options:--c++17
// C++17: the right operand of an assignment is evaluated before the left.
extern "C" int printf(const char *, ...);
int idx() { static int n = 0; return n++; }
struct S { int f; };
int main() {
  int a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  int i = 1;
  int *p = a;
  a[i] = i++;                    /* stores to a[2] */
  printf("%d %d %d %d\n", a[1], a[2], i, a[0]);
  i = 1;
  p[i] = i++ * 10;               /* p[2] = 10 */
  printf("%d %d\n", a[1], a[2]);
  p = a;
  int k = 1;
  p += k++;                      /* p = a + 1 */
  printf("%d %d\n", (int)(p - a), k);
  p = a;
  p -= (k = 0);
  printf("%d\n", (int)(p - a));
  S s[3] = { { 0 }, { 0 }, { 0 } };
  S *q = s;
  q->f = (q = s + 1, 5);         /* the value first: q is s + 1 */
  printf("%d %d\n", s[0].f, s[1].f);
  int j = 0;
  a[j] = a[j + 1] + idx();
  printf("%d\n", a[0]);
  return 0;
}
