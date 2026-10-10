//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct S { int x; char c; short arr[3]; };
struct B { unsigned a : 3; unsigned : 2; unsigned b : 5; int z; };
union U { int i; float f; double d; };
struct Fwd;
struct Fwd *fwd_ptr;
struct S gs;
struct B gb;
union U gu;
typedef int (*fn_t)(int, double);
fn_t gfn;
int gmat[2][3];
enum E { E0, E1 = 5 };
enum E ge;
long double gld;
_Bool gbool;
int use_types(struct S *s, struct B *b) { return s->x + (int)b->a + gmat[1][2]; }
