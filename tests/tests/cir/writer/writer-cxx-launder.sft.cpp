//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_launder (std::launder) is the identity on the pointer.
struct S { int a; };
S *f(S *p) { return __builtin_launder(p); }
int g(S *p) { return __builtin_launder(p)->a; }
