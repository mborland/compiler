//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A range designator followed by more initializers (GCC lays the tail out
   at the last element of the range). */
struct elem { int a[3]; int b; };
struct elem table[4] = { [0 ... 1].a[1 ... 2] = 4, 5 };
