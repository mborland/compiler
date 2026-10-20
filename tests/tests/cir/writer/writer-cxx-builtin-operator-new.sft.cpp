//type:fp
//options:--c++17 --clang --clang_version=190000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// __builtin_operator_new and __builtin_operator_delete (Clang mode) are calls
// of the global allocation functions that the consumer treats as it does a
// new-expression: the CALL carries ALLOC_EXPR.  A direct call of ::operator
// delete does not.
void *f(unsigned long n) { return __builtin_operator_new(n); }
void g(void *p) { __builtin_operator_delete(p); }
void h(void *p, unsigned long n) { ::operator delete(p, n); }
