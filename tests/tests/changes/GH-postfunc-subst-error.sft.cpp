//type:fn
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A registered attribute whose argument cannot be substituted for an instance is an error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:16
//match_regex:(line 34: error: bad\s+attribute\s+argument\s+substitution|line 37: error: bad\s+attribute\s+argument\s+substitution|line 40: error: bad\s+attribute\s+argument\s+substitution|line 43: error: (?:bad\s+attribute\s+argument\s+substitution|an\s+"auto"\s+parameter\s+of\s+a\s+lambda\s+cannot\s+be\s+used)|line 46: error: bad\s+attribute\s+argument\s+substitution|line 49: error: bad\s+attribute\s+argument\s+substitution|line 52: error: expression\s+must\s+have\s+class\s+type\s+but\s+it\s+has\s+type\s+"long"|line 55: error: bad\s+attribute\s+argument\s+substitution|line 58: error: bad\s+attribute\s+argument\s+substitution|line 61: error: identifier\s+"x"\s+is\s+undefined|line 64: error: a\s+parameter\s+is\s+not\s+allowed|line 67: error: bad\s+attribute\s+argument\s+substitution|line 71: error: identifier\s+"U"\s+is\s+undefined|line 74: error: bad\s+attribute\s+argument\s+substitution|line 77: error: bad\s+attribute\s+argument\s+substitution|line 80: error: bad\s+attribute\s+argument\s+substitution)
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template, or of a member of
// a class template, are copied for each instance with the template arguments
// substituted.  A lambda in one is scanned again, and a failure in that scan
// leaves an error expression: that is an error at the attribute (bad attribute
// argument substitution), whatever the cause, never an attribute with an error
// expression and no diagnostic.  A lambda that is generic or has template
// parameters of its own (cases 1 to 4, 8, 9, 12, 15, 16), a function parameter
// or the result name in the lambda (cases 5, 6, 10, 11) and a member template
// of a class template (cases 13 and 14) are not supported and are such an
// error (and not the crashes that some of them were: case 12 is a generic
// lambda with two auto parameters in a function template whose first template
// parameter is a template template parameter; it crashed the front end).  A
// lambda that names a template parameter of a function template or of a member
// template of a class that is not a template is supported (see
// GH-postfunc-lambda-t).  Cases 13 and 14 are the ones that a later change
// supports (the diagnostics of today are pinned here).  An expression that
// cannot be substituted in the attribute of a member of a class template is
// also an error and not an assertion in the front end.  Every case has its own
// diagnostic; the expression above has an alternative for each case, anchored
// to its line.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> int f(T x) Q([](auto a, auto b) -> bool { return a > b; }(x, x));
int u() { return f(1); }
#elif TEST_NUMBER == 2
template<class T> int f(T x) Q([](auto a, int b) { return a > b; }(x, 1));
int u() { return f(1); }
#elif TEST_NUMBER == 3
template<class T> int f(T x) Q([](auto... a) -> bool { return sizeof...(a) == 0; }() && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 4
template<class T> int f(T x) Q([](auto a, auto... b) -> bool { return a > 0; }(x));
int u() { return f(1L); }
#elif TEST_NUMBER == 5
template<class T> T f(T x) E(r, [](T y) -> decltype(y + r + x) { return y; }(x) == r);
int u() { return f(1); }
#elif TEST_NUMBER == 6
template<class T> int f(T x) Q([](decltype(x) y) { return y > 0; }(x));
int u() { return f(1); }
#elif TEST_NUMBER == 7
template<class T> struct S { int m(T x) Q(x.foo() > 0); };
int u() { S<long> s; return s.m(1L); }
#elif TEST_NUMBER == 8
struct S { template<class U> int m(U x) Q([]<class... V>(V... a) -> bool { return true; }() && x > 0); };
int u() { S s; return s.m(1L); }
#elif TEST_NUMBER == 9
int f(auto x) Q([]<class... V>(V... a) -> bool { return true; }() && x > 0);
int u() { return f(1L); }
#elif TEST_NUMBER == 10
template<class T> int f(T x) Q([]{ return sizeof(x) > 0; }());
int u() { return f(1); }
#elif TEST_NUMBER == 11
template<class T> int f(T x) E(r, [&]{ return r > 0; }());
int u() { return f(1); }
#elif TEST_NUMBER == 12
template<template<class> class TT> int f(int x) Q([](auto a, auto b) { return a > b; }(x, 0));
template<class U> struct W {};
int u() { return f<W>(1); }
#elif TEST_NUMBER == 13
template<class T> struct C { template<class U> int m(U x) Q([](U y) { return y > 0; }(x)); };
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 14
template<class T> struct C { template<class U> int m(U x) Q([](T y) { return y > 0; }(x)); };
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 15
template<class T> int f(T x) Q([]<int N>() { return N > 0; }.template operator()<1>() && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 16
template<class T> int f(T x) Q([]<class V>() -> bool { return true; }.template operator()<T>() && x > 0);
int u() { return f(1); }
#endif
