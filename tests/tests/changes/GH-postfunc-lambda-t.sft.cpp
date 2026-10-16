//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A lambda in a contract of a class template member or of a function template
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:15
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template, or of a member of
// a class template, are copied for each instance with the template arguments
// substituted, and a lambda in one is scanned again.  That works for a lambda
// in the attribute of a member of a class template (its parameter may have the
// type T), and for a lambda in the attribute of a function template, or of a
// member template of a class that is not a template, that names a template
// parameter (in the type of its parameters or in its body): cases 7 to 15,
// that were errors ("identifier "T" is undefined") before the bug 0020 fix of
// the front end, which binds the template parameters to the arguments for the
// scan of the lambda.  The lambda of each instance has the types of the
// instance (it was checked with --il_display for the cases that have one
// instance of each kind).  A lambda that is generic or has template
// parameters of its own, and the names that the scan does not find (a function
// parameter in the body, the result name in the lambda, a member template of a
// class template), are errors: see GH-postfunc-subst-error.
// (--no_il_lowering: the configuration without IL lowering is the one that
// front ends that use the IL read.)

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> struct S { T m(T x) Q([](T y) { return y > 0; }(x)); };
long u() { S<long> s; return s.m(1L); }
#elif TEST_NUMBER == 2
template<class T> T f(T x) E(sizeof(T) > 0 && x > 0);
long u() { return f(1L); }
#elif TEST_NUMBER == 3
template<class T> T f(T x) E(r, static_cast<T>(r) > x);
long u() { return f(1L); }
#elif TEST_NUMBER == 4
template<class T> T f(T x) Q([](long y) { return y > 0; }(x));
long u() { return f(1L); }
#elif TEST_NUMBER == 5
template<class T> T f(T x) E(r, [](long y, int z) { return y > z; }(r, 1));
long u() { return f(1L); }
#elif TEST_NUMBER == 6
template<class T, class U> T f(T x, U y) Q([](long a, long b) { return a > b; }(x, y));
long u() { return f(1L, 2); }
#elif TEST_NUMBER == 7
template<class T> T f(T x) Q([](T y) { return y > 0; }(x));
long u() { return f(1L) + f(2); }
#elif TEST_NUMBER == 8
template<class T> T f(T x) E(r, [](T y) { return y > 0; }(r));
long u() { return f(1L); }
#elif TEST_NUMBER == 9
template<class T> int f(T x) Q([](T a, T b) { return a > b; }(x, x));
int u() { return f(1); }
#elif TEST_NUMBER == 10
template<class T, class U> T f(T x, U y) Q([](T a, U b) { return a > b; }(x, y));
long u() { return f(1L, 2); }
#elif TEST_NUMBER == 11
struct S { template<class U> int m(U x) Q([](U y) { return y > 0; }(x)); };
int u() { S s; return s.m(1L) + s.m(2); }
#elif TEST_NUMBER == 12
// The body of the lambda names a non-type template parameter.
template<int N> int f(int x) Q([](int y) { return y > N; }(x));
int u() { return f<3>(4) + f<5>(6); }
#elif TEST_NUMBER == 13
// An explicit specialization without the attribute has the contract of the
// template.
template<class T> T f(T x) Q([](T y) { return y > 0; }(x));
template<> long f<long>(long x) { return x; }
long u() { return f(1L) + f(2); }
#elif TEST_NUMBER == 14
// The instance exists before the definition that has the contract.
template<class T> T f(T x);
long u() { return sizeof(f(1L)); }
template<class T> T f(T x) Q([](T y) { return y > 0; }(x)) { return x; }
#elif TEST_NUMBER == 15
// A member template defined outside its class, with the contract on the
// definition.
struct S { template<class U> int m(U x); };
template<class U> int S::m(U x) Q([](U y) { return y > 0; }(x)) { return 0; }
int u() { S s; return s.m(1L) + s.m(2); }
#endif
