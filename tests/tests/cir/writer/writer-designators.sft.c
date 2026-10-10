//type:fp
//options:--c11 --gcc
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
struct A { int B; short C[2]; };
union D { int E; double F; struct A G; };
int b[10] = { 10, [4] = 15, [2] = 12, [4] = 14, [7] = 17 };
struct A f[] = { 0, 1, 2, [2].C = 11, 12, 13 };
union D j[] = { [4] = 1, [4].F = 1.0, [1].G.C[1] = 4 };
struct s { int a; struct { int b; int c; }; union { int d; struct { int e; }; }; };
struct s x = { .e = 5, .b = 4, .a = 3, .c = 9 };
struct H { char I[6]; int J; } k[] = { { { "foo" }, 1 }, [0].I[0] = 'b' };
struct T { unsigned a:1; unsigned b:1; unsigned c:3; int d:5; };
union U { struct T q; unsigned i[1]; };
struct S { union U f; };
struct S bs = { .f.q.a = 1, .f.q.c = 5, .f.q.d = -3 };
int dyn(int v) { struct s y = { .e = v, .b = 4, .a = 3, .c = v + 1 }; return y.a + y.c; }
