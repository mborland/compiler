//type:fp
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Explicit specializations of members of templates that are accepted, and those that the front end does not read
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:10
// 10/17/26  [GH #?]
//
// See GH-spec-in-class-mt.  The explicit specializations that are accepted: the
// specialization of a member template that is declared in a class template that
// is never instantiated, whatever its contract (nothing is checked: the front
// end does not read the member template of the template then; cerastes rejects
// these specializations on its own side), and the specializations that have no
// contract and the contract of their template: in a class that is not a
// template, of a function template whose contract is on a friend template
// definition, of a member of a member class template and of a member of a class
// template partial specialization.

#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
// The class template is never instantiated, and the in-class specialization has
// another contract: nothing is checked, as the front end does not read the
// specialization of the member template then.  (cerastes rejects an in-class
// specialization that has a contract, or of a member template that has one,
// on its own.)
template <class T> struct Outer {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
#elif TEST_NUMBER == 2
// The same, the member template has no contract and the specialization adds
// one.
template <class T> struct Outer {
  template <class U> static int f(U y) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
#elif TEST_NUMBER == 3
// The same, no contract on the specialization.
template <class T> struct Outer {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) { return 1; }
};
#elif TEST_NUMBER == 4
// The class template is only named (a pointer to an instance): it is not
// instantiated.
template <class T> struct O {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
O<char> *p;
#elif TEST_NUMBER == 5
// A class that is not a template: an in-class specialization with no contract
// has the contract of the member template.
struct N {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) { return 1; }
};
int x = N::f(3);
#elif TEST_NUMBER == 6
// A member class (not a template) of a class template that is not
// instantiated.
template <class T> struct A { struct B {
  template <class V> static int g(V y) Q(y > 0) { return 0; }
  template <> int g<int>(int y) Q(y > 5) { return 1; } }; };
#elif TEST_NUMBER == 7
// A specialization with no contract of a function template whose contract is on
// a friend template definition that follows it, and a call.
template <class T> int f(T x);
template <> int f<int>(int x) { return 1; }
struct X { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
int u() { return f(3); }
#elif TEST_NUMBER == 8
// The same, the friend template definition is in a class template that is
// instantiated.
template <class T> int f(T x);
template <> int f<int>(int x) { return 1; }
template <class U> struct Y { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
Y<int> y;
int u() { return f(3); }
#elif TEST_NUMBER == 9
// A member function of a member class template of a class template, explicitly
// specialized with no contract.
template <class T> struct A { template <class U> struct B { int m(U y) Q(y > 0); }; };
template <> template <> int A<int>::B<long>::m(long y) { return 1; }
int u() { A<int>::B<long> b; return b.m(3L); }
#elif TEST_NUMBER == 10
// A member function of a partial specialization of a class template,
// explicitly specialized with no contract.
template <class T> struct S { int m(T x) Q(x > 0); };
template <class T> struct S<T*> { int m(T* x) Q(x != nullptr); };
template <> int S<int*>::m(int* x) { return 1; }
int u() { S<int*> s; return s.m(nullptr); }
#endif
