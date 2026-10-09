struct S { int a; int arr[4]; };
struct S gs;
int *gp;
int ga[10];
int h(int, ...);
int fn(struct S s, struct S *ps, int i, long l, unsigned char uc, float f) {
  int r;
  r = ps->a + s.a;
  r += ga[i];
  gp = &ga[3];
  gp = gp + i;
  r = (int)(gp - ga);
  r = ps->arr[2];
  l = (long)f;
  f = (float)l;
  r = uc;
  r = i && l;
  r = i || l;
  r = !i;
  r = -i;
  r = ~i;
  r = i ? 1 : 2;
  r = (i, l);
  r = h(i, l, f, uc);
  s = *ps;
  gs = s;
  r = sizeof(s) + sizeof(ga);
  return r;
}
