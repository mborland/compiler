//type:fn
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A registered attribute whose argument cannot be substituted for an instance is an error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:12
//match_regex:(line 26: error: identifier\s+"T"\s+is\s+undefined|line 29: error: identifier\s+"T"\s+is\s+undefined|line 32: error: identifier\s+"T"\s+is\s+undefined|line 35: error: bad\s+attribute\s+argument\s+substitution|line 38: error: bad\s+attribute\s+argument\s+substitution|line 41: error: bad\s+attribute\s+argument\s+substitution|line 44: error: (?:bad\s+attribute\s+argument\s+substitution|an\s+"auto"\s+parameter\s+of\s+a\s+lambda\s+cannot\s+be\s+used)|line 47: error: identifier\s+"T"\s+is\s+undefined|line 50: error: bad\s+attribute\s+argument\s+substitution|line 53: error: expression\s+must\s+have\s+class\s+type|line 56: error: bad\s+attribute\s+argument\s+substitution|line 59: error: bad\s+attribute\s+argument\s+substitution)
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template, or of a member of
// a class template, are copied for each instance with the template arguments
// substituted.  A lambda in one is scanned again, and a failure in that scan
// leaves an error expression: that is an error at the attribute (bad attribute
// argument substitution), whatever the cause, never an attribute with an error
// expression and no diagnostic.  A lambda that uses a template parameter in the
// type of a parameter, and a generic lambda, in the attribute of a function
// template are not supported and are such an error (and not the crashes that
// some of them were).  An expression that cannot be substituted in the
// attribute of a member of a class template is also an error and not an
// assertion in the front end.  Every case has its own diagnostic; the
// expression above has an alternative for each case, anchored to its line.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> T f(T x) Q([](T y) { return y > 0; }(x));
long u() { return f(1L); }
#elif TEST_NUMBER == 2
template<class T> T f(T x) E(r, [](T y) { return y > 0; }(r));
long u() { return f(1L); }
#elif TEST_NUMBER == 3
template<class T> int f(T x) Q([](T a, T b) { return a > b; }(x, x));
int u() { return f(1); }
#elif TEST_NUMBER == 4
template<class T> int f(T x) Q([](auto a, auto b) -> bool { return a > b; }(x, x));
int u() { return f(1); }
#elif TEST_NUMBER == 5
template<class T> int f(T x) Q([](auto a, int b) { return a > b; }(x, 1));
int u() { return f(1); }
#elif TEST_NUMBER == 6
template<class T> int f(T x) Q([](auto... a) -> bool { return sizeof...(a) == 0; }() && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 7
template<class T> int f(T x) Q([](auto a, auto... b) -> bool { return a > 0; }(x));
int u() { return f(1L); }
#elif TEST_NUMBER == 8
template<class T> T f(T x) E(r, [](T y) -> decltype(y + r + x) { return y; }(x) == r);
int u() { return f(1); }
#elif TEST_NUMBER == 9
template<class T> int f(T x) Q([](decltype(x) y) { return y > 0; }(x));
int u() { return f(1); }
#elif TEST_NUMBER == 10
template<class T> struct S { int m(T x) Q(x.foo() > 0); };
int u() { S<long> s; return s.m(1L); }
#elif TEST_NUMBER == 11
struct S { template<class U> int m(U x) Q([]<class... V>(V... a) -> bool { return true; }() && x > 0); };
int u() { S s; return s.m(1L); }
#elif TEST_NUMBER == 12
int f(auto x) Q([]<class... V>(V... a) -> bool { return true; }() && x > 0);
int u() { return f(1L); }
#endif
