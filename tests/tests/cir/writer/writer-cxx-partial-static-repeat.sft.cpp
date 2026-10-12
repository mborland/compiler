//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A repeat of an aggregate that has a dynamic element (the 4 elements of
// q) cannot be built element by element: the definition is dropped with its
// reason instead of leaving the dynamic element out of the STATIC_INIT of a
// partly constant aggregate.
extern "C" int printf(const char *, ...);
int g() { printf("g\n"); return 7; }
struct Q { int a; int b = g(); };
struct R { int x; Q q[4]; } r2 = { 1 };
int use() { return r2.x; }
