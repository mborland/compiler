#include <stdio.h>
struct S; void use(struct S *); void early(void) { use(0); }
struct S { int x; };
void use(struct S *p) { if (p) p->x = 1; }
int a[]; int b = 5;
union U { int i; float f; };
struct T { unsigned b : 3; };
struct Q { int b; };
struct V { struct Q s; int z; };
const char *w = "x";
int main(void) {
  struct S s; s.x = 0; use(&s);
  a[0] = 1;
  struct T t; t.b = 6; t.b /= -2; t.b %= 4;
  union U u = { .i = 1, .f = 2.0f };
  struct V v = { .s.b = 1, .s = { 2 } };
  printf("%d %d %d %d %g %d\n", s.x, a[0], b, t.b, u.f, v.s.b);
  return 0;
}
