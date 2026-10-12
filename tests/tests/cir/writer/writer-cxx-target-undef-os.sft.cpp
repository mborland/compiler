//type:fp
//options:--c++17 --gnu_version=999999
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The same as writer-target-undef-os.sft.c for C++: the triple is captured
// when the predefined macros are entered.  (GNU mode lets a source undefine a
// predefined macro.)
#undef __linux__
#define _WIN32 1
struct S { int v; S() : v(3) {} };
int answer() { S s; return s.v; }
