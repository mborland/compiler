//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The assignment of an empty class copies nothing (no STORE): an empty base
// shares the address of the first member of its class, and a one-byte store
// through the base would overwrite it (std::tuple-like classes).
struct E { };
struct S : E { char x; E &tail() { return *this; } };
void f(S &s, const E &e) { s.tail() = e; }
E &g(E &a, const E &b) { return a = b; }
