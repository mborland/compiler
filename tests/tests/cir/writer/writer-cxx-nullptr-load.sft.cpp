//type:fp
//options:--c++17 --g++ --gnu_version=140000
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A std::nullptr_t is never loaded: its lvalue-to-rvalue conversion gives
// nullptr even when the object was not initialized ([conv.lval]), and clang
// does not read it, so the writer drops the address and sends CONST_NULL (the
// consumer need not treat a nullptr_t value as possibly undefined). A volatile
// nullptr_t is still read.
typedef decltype(nullptr) nullptr_t;
void f(nullptr_t);
struct S { nullptr_t m; };
nullptr_t gn;
nullptr_t get();
void uninit() { nullptr_t n; f(n); }
void param(nullptr_t p) { f(p); }
void global() { f(gn); }
void member(S &s) { f(s.m); }
void initialized() { nullptr_t a = get(); f(a); }
void vol() { volatile nullptr_t v = nullptr; f(v); }
