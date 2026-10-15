//type:fp
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A lambda in a contract of a function template whose parameter has a template parameter type
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:8
// 10/15/26  [GH #?]
//
// The expressions of the attributes of a function template are copied for each
// instance with the template arguments substituted, and a lambda in one is
// scanned again, which needs the template parameters to be visible: a lambda
// with a parameter of the type T used to be an error ("identifier T is
// undefined") when the attribute was copied.  The lambda of a generic lambda
// with a pack of its own is not a function parameter pack of the declaration.
// (This needs IL lowering: with the C++ generating back end the attribute is
// written with the lambda body and read again.)

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> T f(T x) Q([](T y) { return y > 0; }(x));
long u() { return f(1L); }
#elif TEST_NUMBER == 2
template<class T> T f(T x) E([](T y) { return y > 0; }(x));
long u() { return f(1L); }
#elif TEST_NUMBER == 3
template<class T> T f(T x) E(r, [](T y) { return y > 0; }(r));
long u() { return f(1L); }
#elif TEST_NUMBER == 4
template<class T> T f(T x) E(sizeof(T) > 0 && x > 0);
long u() { return f(1L); }
#elif TEST_NUMBER == 5
template<class T> T f(T x) E(r, static_cast<T>(r) > x);
long u() { return f(1L); }
#elif TEST_NUMBER == 6
template<class T> int f(T x) Q([](auto... a) { return sizeof...(a) == 1; }(x) && x > 0);
int u() { return f(1); }
#elif TEST_NUMBER == 7
template<class T, class U> T f(T x, U y) Q([](T a, U b) { return a > b; }(x, y));
long u() { return f(1L, 2); }
#elif TEST_NUMBER == 8
template<class T> struct S { T m(T x) Q([](T y) { return y > 0; }(x)); };
long u() { S<long> s; return s.m(1L); }
#endif
