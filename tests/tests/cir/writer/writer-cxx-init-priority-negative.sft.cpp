//type:fn
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A variable with an init_priority attribute cannot be translated: it is an
// error, and the compilation ends with the front end's error status after the
// definitions that were sent (the names are not mangled again after an
// error: the mangler gives every type a bogus encoding then).
static inline unsigned swap1(unsigned x) { return x ^ 1u; }
struct F { F(); int v; };
F::F() : v(swap1(2u)) { }
static F foo __attribute__((init_priority(5000)));
F zoo __attribute__((init_priority(300)));
int main() { return swap1(3u) != 2u; }
