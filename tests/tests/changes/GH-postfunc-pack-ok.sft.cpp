//type:fp
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Contracts that involve function parameter packs and lambdas that are accepted
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:20
// 10/15/26  [GH #?]
//
// Valid contracts (a registered attribute after the parameter list) that
// involve function parameter packs or lambdas are accepted: a reference to a
// parameter before the pack, a pack of the enclosing template that the
// contract does not name, the pack of a generic lambda written in the contract
// (it is not a parameter pack of the declaration), a pack in a statement.
// (Contracts that refer to the pack, or to a parameter after it, are errors:
// see GH-postfunc-pack-once, GH-postfunc-err-after-pack.)

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
// reference to a parameter BEFORE the pack: valid
template<class... U> int f(int a, U... us) Q(a > 0) E(r, r > a);
int u() { return f(1, 2L, 3.0); }
#elif TEST_NUMBER == 2
// default argument on a parameter before the pack
template<class... U> int f(int a = 1, U... us) Q(a > 0);
int u() { return f(); }
#elif TEST_NUMBER == 3
// abbreviated template, trailing non-pack parameter, no pack
int f(auto x, int y) Q(y > 0) E(r, r > y);
int u() { return f(1L, 2); }
#elif TEST_NUMBER == 4
// abbreviated template, pack last, reference to earlier param
int f(int y, auto... xs) Q(y > 0);
int u() { return f(1, 2L, 3.0); }
#elif TEST_NUMBER == 5
// pack inside a nested function type parameter: y does not follow a FUNCTION parameter pack
template<class... U> int f(int (*g)(U..., int), int y) Q(y > 0);
int h(long, int);
int u() { return f<long>(h, 1); }
#elif TEST_NUMBER == 6
// C varargs, not a pack
int f(int x, ...) Q(x > 0) E(r, r > x);
#elif TEST_NUMBER == 7
// template parameter pack (non-type) but no function parameter pack
template<int... N> int f(int y) Q(y > (int)sizeof...(N));
int u() { return f<1, 2>(5); }
#elif TEST_NUMBER == 8
// class template pack expansion, parameter before the pack
template<class... T> struct S { int m(int a, T... xs) Q(a > 0); };
int u() { S<int, long> s; return s.m(1, 2, 3L); }
#elif TEST_NUMBER == 9
// pack in a class template member, out-of-class definition, reference to a param before the pack
template<class... T> struct S { int m(int a, T... xs); };
template<class... T> int S<T...>::m(int a, T... xs) Q(a > 0) { return a; }
int u() { S<int> s; return s.m(1, 2); }
#elif TEST_NUMBER == 10
// generic lambda parameter after a pack in an attribute: lambda param b, not the function's
int f(int x) Q([](auto... a, int b) { return b > 0; }(1) && x > 0);
#elif TEST_NUMBER == 11
// function template with a pack, unnamed ensures referring to a param before
template<class... U> int f(int a, U... us) E(a > 0);
int u() { return f(1, 2L); }
#elif TEST_NUMBER == 12
// member template pack in class template, parameter before the pack
template<class T> struct S { template<class... U> T g(T y, U... us) E(r, r > y); };
int u() { S<int> s; return s.g(3, 1L, (short)2); }
#elif TEST_NUMBER == 13
// requires-expression in an attribute with a parameter list
template<class... U> int f(int a, U... us) Q(requires (int z) { z + a; } && a > 0);
int u() { return f(1, 2L); }
#elif TEST_NUMBER == 14
// a parameter after a pack that is NOT referenced, attribute references a param before
template<class... U> int f(int a, U... us, int y) Q(a > 0);
int u() { return f<long>(1, 2L, 3); }
#elif TEST_NUMBER == 15
// pack of a class template expanded into a member, then a param; contract on a param before
template<class... T> struct S { int m(int a, T... xs, int y) Q(a > 0); };
int u() { S<long> s; return s.m(1, 2L, 3); }
#elif TEST_NUMBER == 16
// noexcept with sizeof... of the function pack: allowed in noexcept (no attribute), plus attr on param before
template<class... U> int f(int a, U... us) noexcept(sizeof...(us) < 3) Q(a > 0);
int u() { return f(1, 2L); }
#elif TEST_NUMBER == 17
// friend in class template with a pack, param before
template<class... T> struct S { friend int fr(S, int a, T... xs) Q(a > 0) { return a; } };
int u() { return fr(S<int>(), 1, 2); }
#elif TEST_NUMBER == 18
// valid: definition, pack last, attribute refers only to a param before the pack; instance with 2 elements
template<class... T> int f(int a, T... xs) Q(a > 0) E(r, r >= a) { return a; }
int u() { return f(1, 2, 3); }
#elif TEST_NUMBER == 19
// valid: class template member defined in class, pack last, param before
template<class... T> struct S { int m(int a, T... xs) Q(a > 0) { return a; } };
int u() { S<int, int> s; return s.m(1, 2, 3); }
#elif TEST_NUMBER == 20
// valid: definition, pack last, attribute refers to nothing
template<class... T> int f(T... xs) Q(true) { return 0; }
int u() { return f(1, 2, 3); }
#endif
