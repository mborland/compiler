//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __sync_* builtins. */
int add(int *p, int v) { return __sync_fetch_and_add(p, v); }
int cas(int *p, int a, int b) { return __sync_bool_compare_and_swap(p, a, b); }
