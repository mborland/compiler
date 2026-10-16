//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] An explicit specialization of a member template that is declared in its class, or of a member of a class template that has attributes, is checked against the template
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:15
//match_regex:(line 28: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 35: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 43: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 51: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 59: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 66: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 73: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 79: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 85: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 91: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 97: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 102: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 109: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 115: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 122: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it)
// 10/17/26  [GH #?]
//
// See GH-spec-in-class and GH-postfunc-spec.  An explicit specialization of a member
// template of a class template that is declared in its class (CWG 727), when the
// class template is instantiated, is an error if the member template has a
// contract (the specialization of a member template of a class template is not
// supported then) or if the specialization has one and the member template does
// not.  The same specialization in a class that is not a template, or of a
// function template with a contract that is supplied by a friend template
// definition, or of a member of a member class template or of a class template
// partial specialization, is compared with the contract of its template as for
// any explicit specialization.  Every case has its own diagnostic; the
// expression above has an alternative for each case, anchored to its line.

#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
// An in-class explicit specialization (CWG 727) of a member template of a class
// template that is instantiated, with a contract that differs from the one of
// the member template.
template <class T> struct Outer {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
int x = Outer<char>::f(3);
#elif TEST_NUMBER == 2
// The same with no contract on the specialization.
template <class T> struct Outer {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) { return 1; }
};
int x = Outer<char>::f(3);
#elif TEST_NUMBER == 3
// The same, the member template has no contract and the specialization adds
// one.
template <class T> struct Outer {
  template <class U> static int f(U y) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
int x = Outer<char>::f(3);
#elif TEST_NUMBER == 4
// A class that is not a template: the in-class specialization has another
// contract.
struct N {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
int x = N::f(3);
#elif TEST_NUMBER == 5
// A class that is not a template: the member template has no contract and the
// specialization adds one.
struct N {
  template <class U> static int f(U y) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
int x = N::f(3);
#elif TEST_NUMBER == 6
// The class template is instantiated by an explicit instantiation.
template <class T> struct O {
  template <class U> static int f(U y) Q(y > 0) { return 0; }
  template <> int f<int>(int y) Q(y > 5) { return 1; }
};
template struct O<char>;
#elif TEST_NUMBER == 7
// A member class template of a class template.
template <class T> struct A { template <class U> struct B {
  template <class V> static int g(V y) Q(y > 0) { return 0; }
  template <> int g<int>(int y) Q(y > 5) { return 1; } }; };
int x = A<char>::B<char>::g(3);
#elif TEST_NUMBER == 8
// A member class (not a template) of a class template.
template <class T> struct A { struct B {
  template <class V> static int g(V y) Q(y > 0) { return 0; }
  template <> int g<int>(int y) Q(y > 5) { return 1; } }; };
int x = A<char>::B::g(3);
#elif TEST_NUMBER == 9
// A member function of a member class template of a class template, explicitly
// specialized with another contract.
template <class T> struct A { template <class U> struct B { int m(U y) Q(y > 0); }; };
template <> template <> int A<int>::B<long>::m(long y) Q(y > 5) { return 1; }
#elif TEST_NUMBER == 10
// A member function of a partial specialization of a class template,
// explicitly specialized with another contract.
template <class T> struct S { int m(T x) Q(x > 0); };
template <class T> struct S<T*> { int m(T* x) Q(x != nullptr); };
template <> int S<int*>::m(int* x) Q(x == nullptr) { return 1; }
#elif TEST_NUMBER == 11
// A function template whose contract is on a friend template definition that
// follows the explicit specialization (which has another contract).
template <class T> int f(T x);
template <> int f<int>(int x) Q(x > 5) { return 1; }
struct X { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
#elif TEST_NUMBER == 12
// The friend template definition comes first, then the specialization.
template <class T> int f(T x);
struct X { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
template <> int f<int>(int x) Q(x > 5) { return 1; }
#elif TEST_NUMBER == 13
// The friend template definition is in a class template that is instantiated,
// and the specialization follows.
template <class T> int f(T x);
template <class U> struct Y { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
Y<int> y;
template <> int f<int>(int x) Q(x > 5) { return 1; }
#elif TEST_NUMBER == 14
// The specialization comes first, then the friend template definition in a class
// template that is instantiated.
template <class T> int f(T x);
template <> int f<int>(int x) Q(x > 5) { return 1; }
template <class U> struct Y { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
Y<int> y;
#elif TEST_NUMBER == 15
// The same, the class template with the friend definition is never
// instantiated: the specialization has a contract that its template does not
// have yet.
template <class T> int f(T x);
template <> int f<int>(int x) Q(x > 5) { return 1; }
template <class U> struct Y { template <class T> friend int f(T x) Q(x > 0) { return 0; } };
#endif
