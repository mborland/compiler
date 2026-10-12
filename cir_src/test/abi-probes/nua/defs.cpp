#include "p.h"
int f_n1(N1 s) { return s.x; }
N1 make_n1(int v) { return N1(v); }
N1 pass_n1(N1 s) { return s; }
int f_n2(N2 s) { return s.x; }
N2 make_n2(int v) { return N2(v); }
int f_n3(N3 s) { return s.x; }
N3 make_n3(int v) { return N3(v); }
int f_n4(N4 s) { return s.b.a * 10000 + s.b.c * 100 + s.d; }
N4 make_n4(int a, char c, char d) { return N4(a, c, d); }
N4 pass_n4(N4 s) { return s; }
int f_n5(N5 s) { return (int)s.l.l * 10000 + s.l.c * 1000 + s.d * 100 + s.e; }
N5 make_n5(long l, char c, char d, short e) { return N5(l, c, d, e); }
N5 pass_n5(N5 s) { return s; }
int f_n6(N6 s) { return s.b.a * 10000 + s.b.c * 100 + s.d; }
N6 make_n6(int a, char c, char d) { return N6(a, c, d); }
int f_n7(N7 s) { return (int)s.f; }
N7 make_n7(double v) { return N7(v); }
int f_n8(N8 s) { return (int)s.l; }
N8 make_n8(long v) { return N8(v); }
int f_mix(int a, N1 p, N4 q, N5 r, N7 s, N8 t, int z) {
  return a + p.x + q.b.a + q.b.c + q.d + (int)r.l.l + r.l.c + r.d + r.e +
         (int)s.f + (int)t.l + z;
}
