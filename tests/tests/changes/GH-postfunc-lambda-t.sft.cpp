//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A lambda in a contract of a class template member or of a function template
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:6
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template, or of a member of
// a class template, are copied for each instance with the template arguments
// substituted, and a lambda in one is scanned again.  That works for a lambda
// in the attribute of a member of a class template (its parameter may have the
// type T), and for a lambda in the attribute of a function template whose
// parameters do not involve a template parameter.  A lambda whose parameter
// has the type of a template parameter of a function template, and a generic
// lambda in the attribute of a function template, are not supported: see
// GH-postfunc-subst-error.  (--no_il_lowering: the configuration without IL
// lowering is the one that front ends that use the IL read.)

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
#endif
