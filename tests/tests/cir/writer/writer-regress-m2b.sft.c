//type:fp
//options:--c23 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A; void ta(struct A *);
void f1(void) { ta(0); }
struct A { _Atomic int n; };
int g(void) { return 1; }
struct B; void tb(struct B *);
void f2(void) { tb(0); }
struct B { _BitInt(7) f : 3; int z; };
int g2(void) { return 2; }
int fn(void) { static const char *w = __func__; static const char *n[] = { "x", __func__ }; return w[0] + n[1][0]; }
struct Q { int a, b; };
struct V { struct Q s; };
struct V v = { .s.b = 1, .s = { 2 } };
