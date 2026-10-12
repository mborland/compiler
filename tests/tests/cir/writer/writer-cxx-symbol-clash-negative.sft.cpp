//type:fn
//options:--c++14
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// An extern "C" function whose name is the mangled name of a constructor,
// defined after the constructor was used (gnu/cpp/pr77550): the stream has
// one G-id per symbol, so the definition cannot be sent.  An external
// definition that no other translation unit can supply must not vanish: it
// is an error, not a warning (the library could not be told).
struct T { T(char *); int v; };
int use_t() { T t(0); return t.v; }
extern "C" void _ZN1TC1EPc(...) {}
