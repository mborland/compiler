//type:fn
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A gnu_inline (inline-only) body followed by a real definition with the same
// asm label: the real definition is the second body of one symbol, an error.
extern int g1(int x) __asm("shared_label");
extern int g2(int x) __asm("shared_label");
extern __inline __attribute__((__always_inline__, __gnu_inline__)) int
g1(int x) { return x + 1; }
int g2(int x) { return x + 2; }
int use() { return g1(1) + g2(2); }
