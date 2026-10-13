//type:fp
//options:--c++14
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A dynamic exception specification: FUNC_EH_SPEC dynamic with the typeinfo of
// each listed type (in C++14, where throw() is dynamic with an empty list,
// not noexcept).
struct E { int e; };
void g();
void d1() throw(int, E) { g(); }
void d2() throw(E *, const char *) { g(); }
void d3() throw() { g(); }
void d4() throw(int, int, long) { g(); }
