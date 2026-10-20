//type:fp
//options:--c++17
//require:BACK_END_TYPE 3
//script:edg-cir-ecil-dump
// The static invoker of a lambda passes a local of the closure type as the
// object of the call (spec 11.9, clang's "unused.capture"), not a null
// pointer: the object pointer of a call is never null (nonnull_ptrs).  The
// local is a LOCAL of the closure type, declared after the parameters.
struct R { R(); R(const R &); ~R(); int v; };
int plain() { auto l = [](int x, int y) { return x + y; }; int (*p)(int, int) = l; return p(1, 2); }
double generic() { auto g = [](auto v) { return v * 2; }; double (*q)(double) = g; return q(1.5); }
int refs() { auto l = [](int &r, const int &c) { return r + c; }; int (*p)(int &, const int &) = l; int a = 1; return p(a, 2); }
R by_value() { auto l = [](int x) { R r; r.v = x; return r; }; R (*p)(int) = l; return p(3); }
void unit() { auto l = []() {}; void (*p)() = l; p(); }
