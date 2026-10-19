//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// g++ honors noinline that a later declaration adds after the definition (the
// call stays); clang ignores it with a warning.  The writer sends it at the
// last issue, as g++ does: the declaration is issued again with key 14.
int f(int x) { return x + 1; }
int f(int x) __attribute__((noinline));
int g(int y) { return f(y) * 2; }
