//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* alias. */
int target(void) { return 1; }
int other(void) __attribute__((alias("target")));
int use(void) { return other(); }
