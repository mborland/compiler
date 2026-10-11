//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int lam_default(int (*f)(int) = [](int x) { return x * 2; }) { return f(2); }
struct HasDefault { int (*fp)(int) = [](int x) { return x; }; int k = [] { return 7; }(); };
int hd() { HasDefault h; return h.k + h.fp(1); }
int use() { return lam_default() + hd(); }
int lam(int p) { auto l = [p](int z) { static int s2 = 0; return z + p + s2++; }; return l(1); }
