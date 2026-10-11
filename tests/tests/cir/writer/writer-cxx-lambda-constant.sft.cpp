//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
int (*p)(int) = [](int x) { return x; };
struct S { static int (*q)(int); };
int (*S::q)(int) = [](int x) { return x + 1; };
