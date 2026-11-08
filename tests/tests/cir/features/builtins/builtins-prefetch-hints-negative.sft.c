//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* __builtin_prefetch and the other hint builtins. */
int touch(int *p) { __builtin_prefetch(p, 0, 3); return *p; }
