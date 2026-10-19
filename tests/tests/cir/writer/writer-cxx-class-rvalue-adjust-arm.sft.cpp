//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// A conditional arm that is a cv-adjusted class temporary built in place
// leaves no value on the stack: no DROP follows it (init/ref16).  A class
// glvalue adjusted to a prvalue is loaded, so the DROP stays.
struct A { A(); A(const A &); ~A(); };
struct T { int i; };
A f();
T ft();
const A *ap;
const T *tp;
void bind(bool c) { const A &r = c ? *ap : f(); (void)r; }
void bind_trivial(bool c) { const T &r = c ? *tp : ft(); (void)r; }
void discard(bool c) { (void)(c ? *ap : f()); }
void discard_trivial(bool c) { (void)(c ? *tp : ft()); }
int comma(bool c) { return (c ? *ap : f(), 3); }
