//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* Vector builtins (and the vector types they need). */
typedef int v4si __attribute__((vector_size(16)));
v4si shuffle(v4si a, v4si b) { return __builtin_shuffle(a, b, (v4si){0, 4, 1, 5}); }
