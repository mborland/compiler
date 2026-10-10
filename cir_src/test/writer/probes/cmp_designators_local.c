#include <stdio.h>
struct s { int a; struct { int b, c; }; union { int d; struct { int e; }; }; };
struct H { char I[6]; int J; };
__attribute__((noinline)) int f(int v) { struct s y = { .e = v, .b = 4, .a = 3, .c = 9 }; return y.c * 100 + y.b * 10 + y.e + y.a; }
__attribute__((noinline)) int g(int v) { struct H h = { .I = "foo", .J = v, .I[0] = 'b' }; return h.I[0] * 1000 + h.I[1] * 10 + h.J; }
int main(void) { printf("%d %d\n", f(5), g(7)); return 0; }
