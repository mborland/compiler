//type:fp
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A generic lambda in a function template's signature is substituted as before
//cases:9
// 10/15/26  [GH #?]
//
// A generic lambda (an "auto" parameter, or template parameters) in the declaration
// of a function template, outside of an attribute, is substituted when the
// template is instantiated: in a decltype return type, a requires-clause, a
// default template argument, a requires-expression, explicit(bool), with a
// lambda that has a template parameter list, and in SFINAE.  The copy of a
// generic lambda in the expression of an attribute for an instance is an error
// (GH-postfunc-subst-error): this test makes sure that it is only there.  The
// last case checks that the overload that is chosen is the right one (the
// candidate with the decltype is viable, so it is better than the ellipsis).

#if TEST_NUMBER == 1
template<class T> auto f(T t) -> decltype([](auto x){ return x; }(t)) { return t; }
long u() { return f(1L); }
#elif TEST_NUMBER == 2
template<class T> requires ([](auto x){ return sizeof(x) > 0; }(T{})) int f(T) { return 1; }
int u() { return f(1L); }
#elif TEST_NUMBER == 3
template<class T, int N = [](auto x){ return (int)sizeof(x); }(T{})> int f(T) { return N; }
int u() { return f(1L); }
#elif TEST_NUMBER == 4
template<class T> auto f(T t) -> decltype([]<class U>(U x){ return x; }(t)) { return t; }
long u() { return f(1L); }
#elif TEST_NUMBER == 5
template<class T> requires requires(T t) { [](auto x){ return x; }(t); } int f(T) { return 1; }
int u() { return f(1L); }
#elif TEST_NUMBER == 6
struct S { template<class T> explicit([](auto x){ return sizeof(x) > 4; }(T{})) S(T) {} };
S u() { return S(1); }
#elif TEST_NUMBER == 7
template<class T> auto f(T t) -> decltype([](auto x){ return x; }(t), char{}) { return 'a'; }
long f(...) { return 0; }
static_assert(sizeof(f(5)) == 1, "the generic-lambda overload must be selected");
#elif TEST_NUMBER == 8
template<class T> requires ([](auto x){ return sizeof(x) == 8; }(T{})) char g(T) { return 'a'; }
long g(...) { return 0; }
static_assert(sizeof(g(5L)) == 1, "the constrained overload must be selected");
#elif TEST_NUMBER == 9
template<class T> constexpr auto pick(T t) -> decltype([](auto x){ return x; }(t), 1) { return 1; }
constexpr int pick(...) { return 2; }
static_assert(pick(5) == 1, "the overload with the generic lambda must be chosen");
#endif
