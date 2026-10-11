//type:fp
//options:--c11
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* The same in C: a function type that returns or takes the struct that
   contains the pointer to it. */
struct S { struct S (*f)(void); int x; };
struct S g;
struct T { void (*h)(struct T); int y; };
struct T t;
int main(void) { return g.x + t.y; }
