//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A vector type. */
typedef int v4si __attribute__((vector_size(16)));
v4si add(v4si a, v4si b) { return a + b; }
