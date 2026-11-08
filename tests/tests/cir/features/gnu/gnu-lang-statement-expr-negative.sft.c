//type:fn
//options:--gcc --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
/* A statement expression. */
int twice(int x) { return ({ int y = x; y * 2; }); }
