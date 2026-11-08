//type:fn
//options:--c99 --gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A static initializer of a flexible array member. */
struct flex { int n; int a[]; } flexible = { 2, { 10, 20 } };
int use(void) { return flexible.n; }
