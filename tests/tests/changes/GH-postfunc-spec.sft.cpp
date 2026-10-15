//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Explicit specializations must match the attributes of the template
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:12
//match_regex:(line 25: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 31: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 38: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 44: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 50: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute|line 56: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute|line 61: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 66: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 72: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 78: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 85: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute|line 89: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it)
// 10/14/26  [GH #?]
//
// An explicit specialization of a function template, or of a member of a class
// template, must have the registered attributes that must match (r_pre) of the
// template: the specialization may repeat them or have none, but it may not
// differ, in any order of the declarations, and when the template has none it
// may not add any.  The contract of a member of a class template is the one on
// any declaration of the member, including a definition outside the class
// (the case where it is only there is the point of most of the cases).  Every
// case has its own diagnostic; the expression above has an alternative for
// each case, anchored to the line of its error.

#if TEST_NUMBER == 1
// The contract of a member of a class template is on the definition outside
// the class: an explicit specialization of the member declared after it must
// match it.
template <class T> struct S1 { int m(T x); };
template <class T> int S1<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
template <> int S1<int>::m(int x) [[edg::r_pre(x > 5)]] { return 0; }
#elif TEST_NUMBER == 2
// The same when an instance of the class exists before the definition.
template <class T> struct S2 { int m(T x); };
S2<int> *ps2;
template <class T> int S2<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
template <> int S2<int>::m(int x) [[edg::r_pre(x > 5)]] { return 0; }
#elif TEST_NUMBER == 3
// The same when the instance of the class has been created (not used) after
// the definition.
template <class T> struct S3 { int m(T x); };
template <class T> int S3<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
S3<int> s3;
template <> int S3<int>::m(int x) [[edg::r_pre(x > 5)]] { return 0; }
#elif TEST_NUMBER == 4
// An explicit specialization of the member declared before the definition
// with the contract is compared with it too.
template <class T> struct S4 { int m(T x); };
template <> int S4<int>::m(int x) [[edg::r_pre(x > 1)]] { return 0; }
template <class T> int S4<T>::m(T x) [[edg::r_pre(x > 0)]] { return 0; }
#elif TEST_NUMBER == 5
// A member template: an explicit specialization of an instance of it is not
// supported when the member template has the attribute (here the specialization
// comes first, and the definition with the attribute after it).
template <class T> struct S5 { template <class U> int g(U y); };
template <> template <> int S5<int>::g<long>(long y) [[edg::r_pre(y > 5)]] { return 0; }
template <class T> template <class U> int S5<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
#elif TEST_NUMBER == 6
// A member template: the specialization comes after the definition.
template <class T> struct S6 { template <class U> int g(U y); };
template <class T> template <class U> int S6<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <> int S6<int>::g<long>(long y) [[edg::r_pre(y > 5)]] { return 0; }
#elif TEST_NUMBER == 7
// A function template that has no contract: an explicit specialization cannot
// add one.
template <class T> int f7(T x) { return 0; }
template <> int f7<int>(int x) [[edg::r_pre(x > 1)]] { return 0; }
int u7() { return f7(0); }
#elif TEST_NUMBER == 8
// A member of a class template that has no contract: the same.
template <class T> struct S8 { int m(T x) { return 0; } };
template <> int S8<int>::m(int x) [[edg::r_pre(x > 1)]] { return 0; }
int u8() { S8<int> s; return s.m(0); }
#elif TEST_NUMBER == 9
// A member of a class template with a contract in the class: a different
// contract on the specialization.
template <class T> struct S9 { int m(T x) [[edg::r_pre(x > 0)]]; };
template <> int S9<int>::m(int x) [[edg::r_pre(x > 1)]] { return 0; }
#elif TEST_NUMBER == 10
// A function template whose contract is on a later redeclaration, and a
// specialization that has a different one.
template <class T> int f10(T x);
template <> int f10<int>(int x) [[edg::r_pre(x > 1)]];
template <class T> int f10(T x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 11
// An explicit specialization of a member template of a class template for one
// class (template<> template<class U> ...) must match the contract on the
// definition outside the class.
template <class T> struct S11 { template <class U> int g(U y); };
template <class T> template <class U> int S11<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <class U> int S11<int>::g(U y) [[edg::r_pre(y > 5)]] { return 0; }
#elif TEST_NUMBER == 12
// The same when the member template of the class template has no contract.
template <class T> struct S12 { template <class U> int g(U y); };
template <> template <class U> int S12<int>::g(U y) [[edg::r_pre(y > 5)]] { return 0; }
#endif
