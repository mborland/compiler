//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_add_overflow_p whose last argument is a bit-field is checked
// against the width of the bit-field (gcc): not expressed, so the function is
// dropped with a reason; the same with an ordinary object is translated.
struct A { int i : 1; };
int f(int x, int y) { A a = {}; return __builtin_add_overflow_p(x, y, a.i); }
int g(int x, int y) { int r = 0; return __builtin_add_overflow_p(x, y, r); }
