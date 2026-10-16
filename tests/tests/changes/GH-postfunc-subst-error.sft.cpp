//type:fn
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A registered attribute whose argument cannot be substituted for an instance is an error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:21
//match_regex:(line 35: error: bad\s+attribute\s+argument\s+substitution|line 38: error: bad\s+attribute\s+argument\s+substitution|line 41: error: bad\s+attribute\s+argument\s+substitution|line 44: error: (?:bad\s+attribute\s+argument\s+substitution|an\s+"auto"\s+parameter\s+of\s+a\s+lambda\s+cannot\s+be\s+used)|line 47: error: bad\s+attribute\s+argument\s+substitution|line 50: error: bad\s+attribute\s+argument\s+substitution|line 53: error: expression\s+must\s+have\s+class\s+type\s+but\s+it\s+has\s+type\s+"long"|line 56: error: bad\s+attribute\s+argument\s+substitution|line 59: error: bad\s+attribute\s+argument\s+substitution|line 62: error: identifier\s+"x"\s+is\s+undefined|line 65: error: a\s+parameter\s+is\s+not\s+allowed|line 68: error: bad\s+attribute\s+argument\s+substitution|line 72: error: bad\s+attribute\s+argument\s+substitution|line 75: error: a\s+parameter\s+is\s+not\s+allowed|line 78: error: bad\s+attribute\s+argument\s+substitution|line 81: error: bad\s+attribute\s+argument\s+substitution|line 84: error: bad\s+attribute\s+argument\s+substitution|line 88: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 93: error: function\s+"C<T>::lambda\s+\[\]\(long\s+\*\)\->bool\s+\[with\s+T=int\]"\s+cannot\s+be\s+called\s+with\s+the\s+given\s+argument\s+list|line 97: error: static\s+assertion\s+failed|line 104: error: bad\s+attribute\s+argument\s+substitution)
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template, or of a member of
// a class template, are copied for each instance with the template arguments
// substituted.  A lambda in one is scanned again, and a failure in that scan
// leaves an error expression: that is an error at the attribute (bad attribute
// argument substitution), whatever the cause, never an attribute with an error
// expression and no diagnostic.  A lambda that is generic or has template
// parameters of its own (cases 1 to 4, 8, 9, 12, 15, 16, 17), a function
// parameter or the result name in the lambda (cases 5, 6, 10, 11, 13, 14) are
// not supported and are such an error (and not the crashes that some of them
// were: case 12 is a generic lambda with two auto parameters in a function
// template whose first template parameter is a template template parameter; it
// crashed the front end).  A lambda that names a template parameter of a
// function template, of a member template of a class that is not a template
// or of a member template of a class template is supported (see
// GH-postfunc-lambda-t).  An expression that cannot be substituted in the
// attribute of a member of a class template is also an error and not an
// assertion in the front end (case 7), and so is an explicit specialization
// of a member template of a class template that has the attribute (case 18:
// the diagnostic of the specialization, not of the lambda).  Every case has
// its own diagnostic; the expression above has an alternative for each case,
// anchored to its line.

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
template<class T> struct C { template<class U> int m(U x) Q([](decltype(x) y) { return y > 0; }(x)); };
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 14
template<class T> struct C { template<class U> int m(U x) E(r, [&]{ return r > 0; }()); };
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 15
template<class T> int f(T x) Q([]<int N>() { return N > 0; }.template operator()<1>() && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 16
template<class T> int f(T x) Q([]<class V>() -> bool { return true; }.template operator()<T>() && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 17
template<class T> struct C { template<class U> int m(U x) Q([](auto a) { return a > T{}; }(x)); };
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 18
template<class T> struct C { template<class U> int m(U x) Q([](T a, U b) { return a > b; }(T{}, x)); };
template<> template<> int C<int>::m<long>(long x) { return 0; }
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 19
// The type of the lambda's parameter is not the type of the argument (long* for
// U = char): a real error, as the substitution of U is used.
template<class T> struct C { template<class U> int m(U x) Q([](long* b) { return b != nullptr; }(&x)); };
int u() { C<int> c; return c.m('a'); }
#elif TEST_NUMBER == 20
// The static_assert in the lambda is wrong for the member's own argument.
template<class T> struct C { template<class U> int m(U x) Q([]{ static_assert(sizeof(U) == sizeof(short)); return true; }()); };
template<class U> int g(U u) { C<U> c; return c.m(1L); }
int u() { return g<short>(1); }
#elif TEST_NUMBER == 21
// An explicit instantiation of a member template of a class template, and the
// use of the member template of another instance of the class afterwards: an
// error (the substitution fails closed; not supported yet).
template<class T> struct C { static T st; template<class U> int m(U x) Q([](T* a, U* b) { return a != nullptr && b != nullptr; }(&st, &x)) { return 0; } };
template int C<int>::m<long>(long);
template<class U> struct X { int k() { return C<U>{}.m(1.0f); } };
int u() { X<short> x; return x.k(); }
#endif
