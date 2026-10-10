#include <stdio.h>
struct Q { int a, b; };
struct V { struct Q s; } v = { .s.b = 1, .s = { 2 } };
struct V v2 = { .s.b = 1, .s.a = 2 };
struct H { char I[6]; int J; };
struct H k[] = { { { "foo" }, 1 }, [0].I[0] = 'b' };
struct H m[] = { { { "foo" }, 1 }, [0] = { .I[0] = 'b' } };
int f(void) { static const char *w = __func__; static const char *n[] = { "x", __func__ }; return w[0] + n[1][0]; }
int main(void) {
  printf("%d %d %d %d\n", v.s.a, v.s.b, v2.s.a, v2.s.b);
  printf("%d %d %d | %d %d %d\n", k[0].I[0], k[0].I[1], k[0].J, m[0].I[0], m[0].I[1], m[0].J);
  printf("%d\n", f());
  return 0;
}
