//type:fn
//options:--c23 --gcc --gnu_version=150000 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A floating constant of a type other than float, double and long double. */
_Float16 half_constant(void) { return 1.5; }
_Float128 quad_global = 2.5;
