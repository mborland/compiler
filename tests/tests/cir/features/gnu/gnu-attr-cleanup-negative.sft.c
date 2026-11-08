//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* cleanup. */
static void release(int *p) { *p = 0; }
int use(void) { int x __attribute__((cleanup(release))) = 3; return x; }
