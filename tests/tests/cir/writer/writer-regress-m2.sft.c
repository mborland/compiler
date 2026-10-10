//type:fp
//options:--c99 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct s { int a; struct { int b, c; }; union { int d; struct { int e; }; }; };
struct H { char I[6]; int J; };
int f(int v) { struct s y = { .e = v, .b = 4, .a = 3, .c = 9 }; return y.c; }
int g(int v) { struct H h = { .I = "foo", .J = v, .I[0] = 'b' }; return h.I[1]; }
struct S; void use(struct S *); void early(void) { use(0); }
struct S { int x; };
void use(struct S *p) { if (p) p->x = 1; }
int a[]; int b = 5;
union U { int i; float f; };
union U u = { .i = 1, .f = 2.0f };
struct T { unsigned b : 3; };
int bf(struct T *t) { t->b /= -2; return t->b; }
void kr(); void kr(a) int a; { } void kr(int);
static const char *w = __func__;
static const char *n[] = { "x", __func__ };
