//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The attribute arrives on a later declaration: the function was issued
// without key 14 when it was first needed, and is issued again with it (and
// every later issue carries it).
void f1();
void g1() { f1(); }
void f1() __attribute__((noinline));
void f1() {}
int f2(int);
int g2(int x) { return f2(x); }
int f2(int x) __attribute__((noinline));
int h2(int x) { return f2(x) + 1; }
int f2(int x) { return x; }
