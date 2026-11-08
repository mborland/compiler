//type:fn
//options:--g++ --c++17 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* Function multiversioning (target attributes on overloads, C++). */
__attribute__((target("default"))) int version() { return 0; }
__attribute__((target("avx2"))) int version() { return 2; }
int use() { return version(); }
