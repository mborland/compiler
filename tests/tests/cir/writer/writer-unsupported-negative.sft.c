//type:fn
//options:--c11 --diag_error=cir_warning
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int vla(int n) { int a[n]; a[0] = 1; return a[0]; }
int fine(int n) { return n + 1; }
