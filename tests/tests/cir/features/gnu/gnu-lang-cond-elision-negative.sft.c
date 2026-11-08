//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* a ?: b. */
int pick(int a, int b) { return a ?: b; }
