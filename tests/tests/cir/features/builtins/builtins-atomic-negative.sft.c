//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __atomic_* builtins. */
int load(int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
void store(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
int add(int *p, int v) { return __atomic_fetch_add(p, v, __ATOMIC_RELAXED); }
