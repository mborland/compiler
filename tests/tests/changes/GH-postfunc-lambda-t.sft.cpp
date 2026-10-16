//type:fp
//options_all:--c++20 --instantiate=used --no_il_lowering
//remark:[GH #?] A lambda in a contract of a class template member or of a function template
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//require:DO_IL_LOWERING 1
//cases:29
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
// scan of the lambda.  It works for a member template of a class template too
// (cases 16 to 29), where the lambda can name the parameters of the member
// template (U) and of the class (T): the lambda is left alone while the
// parameters of the class are substituted, and copied when the parameters of
// the member template are, with the parameters of both bound.  The lambda of
// each instance has the types of the instance (it was checked with
// --il_display for the cases that have one instance of each kind).  A lambda
// that is generic or has template parameters of its own, and the names that
// the scan does not find (a function parameter in the body, the result name in
// the lambda), are errors: see GH-postfunc-subst-error.
// (--no_il_lowering: the configuration without IL lowering is the one that
// front ends that use the IL read.)

// Globals that are named like the template parameters and have other sizes: a
// lookup of T or U that did not find the template parameter would find these
// (cases 26 to 29, which check the types and the sizes of the instance).
struct T { char c[3]; };
struct U { char c[5]; };

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
#elif TEST_NUMBER == 16
// The lambda names the parameter of the member template.
template<class T> struct C { template<class U> int m(U x) Q([](U y) { return y > 0; }(x)); };
int u() { C<int> c; return c.m(1L) + c.m(2u); }
#elif TEST_NUMBER == 17
// The lambda names the parameter of the class, and the parameter of the member
// template in the argument that it is called with.
template<class T> struct C { template<class U> int m(U x) Q([](T y) { return y > 0; }(x)); };
int u() { C<int> c; return c.m(1L) + c.m(2u); }
#elif TEST_NUMBER == 18
// Both, in the types of the parameters of the lambda; two instances of the
// class and two of the member template.
template<class T> struct C { template<class U> int m(U x) Q([](T a, U b) { return a > b; }(T{}, x)); };
int u() { C<int> c; C<long> d; return c.m(1L) + c.m(2u) + d.m(3) + d.m(4u); }
#elif TEST_NUMBER == 19
// Both, in the body.
template<class T> struct C { template<class U> int m(U x) Q([]{ return sizeof(T) < sizeof(U); }() && x > 0); };
int u() { C<char> c; C<long> d; return c.m(1L) + d.m(2); }
#elif TEST_NUMBER == 20
// A static member template, and a non-template member of the same class.
template<class T> struct C {
  template<class U> static int m(U x) Q([](U b) { return b > T{}; }(x));
  int k(T x) Q([](T y) { return y > 0; }(x));
};
int u() { C<int> c; return C<int>::m(1L) + C<long>::m(2) + c.k(1); }
#elif TEST_NUMBER == 21
// A member template of a member class template: three levels.
template<class T> struct C {
  template<class U> struct D {
    template<class V> int m(V x) Q([](T a, U b, V c) { return a > b && b > c; }(T{}, U{}, x));
  };
};
int u() { C<int>::D<long> d; return d.m(1L) + d.m(2); }
#elif TEST_NUMBER == 22
// The contract is on the definition outside the class.
template<class T> struct C { template<class U> int m(U x); };
template<class T> template<class U> int C<T>::m(U x) Q([](T a, U b) { return a > b; }(T{}, x)) { return 0; }
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 23
// A constructor template.
template<class T> struct C { template<class U> C(U x) Q([](T a, U b) { return a > b; }(T{}, x)); };
int u() { C<int> c(1L); C<long> d(2); return 0; }
#elif TEST_NUMBER == 24
// The member template is instantiated while other templates that have a
// parameter list of the same shape are being instantiated.
template<class T> struct C { template<class U> int m(U x) Q([](T a, U b) { return a > b; }(T{}, x)); };
template<class U> int g(U) { C<int> c; return c.m(1L) + c.m('a'); }
template<class T> int h(T) { C<T> c; return c.m(1.0f) + c.m(1L); }
int u() { return g<short>(1) + h<char>('x') + h<long>(1L); }
#elif TEST_NUMBER == 25
// A member of a member class template that is not a template: the contract
// names the parameters of both classes.
template<class T> struct C { template<class U> struct D { int k(U x) Q([](T a, U b) { return a > b; }(T{}, x)); }; };
int u() { C<int>::D<long> d; C<char>::D<short> e; return d.k(1L) + e.k(2); }
#elif TEST_NUMBER == 26
// The types of the instance, by pointers (a wrong T or U makes the conversion
// of the argument an error); instances from inside other templates, crossing
// the parameters.
template<class T> struct C {
  static T st;
  template<class U> int m(U x) Q([](T* a, U* b) { return a != nullptr && b != nullptr; }(&st, &x));
};
template<class V> int g(V) { C<int> c; return c.m(1L) + c.m('a'); }
template<class V> int h(V) { C<V> c; return c.m(1.0f) + c.m(1L); }
int u() { return g<short>(1) + h<char>('x') + h<long>(1L); }
#elif TEST_NUMBER == 27
// The sizes of the instance, in a static_assert in the lambda.
template<class T> struct C {
  template<class U> int m(U x) Q([]{ static_assert(sizeof(T) == sizeof(int)); static_assert(sizeof(U) == sizeof(long)); return true; }());
};
int u() { C<int> c; return c.m(1L); }
#elif TEST_NUMBER == 28
// The member template of a class template that is instantiated during the
// deduction of a function template of the same shape.
template<class T> struct C {
  static T st;
  template<class U> int m(U x) Q([](T* a, U* b) { return a != nullptr && b != nullptr; }(&st, &x));
};
template<class V> auto k(V v) -> decltype(C<int>{}.m(1L) + v) { return 0; }
int u() { return k<short>(1); }
#elif TEST_NUMBER == 29
// The parameters of the member template are named like those of the function
// template that instantiates it, crossed.
template<class U> struct C {
  static U st;
  template<class T> int m(T x) Q([](U* a, T* b) { return a != nullptr && b != nullptr; }(&st, &x));
};
template<class T> int g(T t) { C<T> c; return c.m(1L) + c.m(t); }
int u() { return g<char>('a') + g<short>(1); }
#endif
