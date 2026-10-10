//type:fn
//options:--c11
//require:BACK_END_TYPE 3
int vla(int n) { int a[n]; a[0] = 1; return a[0]; }
int ok(int n) { return n + 1; }
