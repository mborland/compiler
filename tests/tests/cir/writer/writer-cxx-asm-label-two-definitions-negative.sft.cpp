//type:fn
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// Two real definitions that share one asm label are one symbol with two
// bodies: an error (gcc fails in the assembler), and the first body stays.
int f1(int x) __asm("shared_symbol");
int f2(int x) __asm("shared_symbol");
int f1(int x) { return x + 1; }
int f2(int x) { return x + 2; }
int use() { return f1(1) + f2(2); }
