//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Explicit specializations of member templates of class templates that have attributes that must match
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:16
//match_regex:(line 26: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 31: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 36: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 42: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 49: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 56: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 61: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 66: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 71: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 77: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 84: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 91: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 96: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 101: error: attribute\s+"edg::r_pre"\s+is\s+on\s+an\s+explicit\s+specialization\s+but\s+the\s+template\s+does\s+not\s+have\s+it|line 106: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre"|line 112: error: an\s+explicit\s+specialization\s+of\s+a\s+member\s+template\s+of\s+a\s+class\s+template\s+is\s+not\s+supported\s+when\s+the\s+member\s+template\s+has\s+the\s+attribute\s+"edg::r_pre")
// 10/15/26  [GH #?]
//
// An explicit specialization of a member template of a class template, for one
// instance of the class, is not supported when the member template has a
// registered attribute that must match those of every declaration (r_pre), in
// the class or on a definition outside it, whether the specialization has a
// contract (the same or a different one) or not, and whichever declaration
// comes first: the attributes of the specialization and of the template cannot
// be compared reliably and a copy cannot be inherited.  When the member
// template has none, the specialization cannot add one.  Both forms of the
// specialization are covered (template<> template<class U> ... g(U), and
// template<> template<> ... g<long>(long)).  Every case has its own diagnostic;
// the expression above has an alternative for each case, anchored to the line
// of its error.

#if TEST_NUMBER == 1
// Form template<> template<class U> ..., the contract of the member template
// in the class, the specialization has no contract.
template <class T> struct S1 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <class U> int S1<int>::g(U y) { return 1; }
#elif TEST_NUMBER == 2
// Form template<> template<class U> ..., the contract of the member template
// in the class, the specialization has the same contract.
template <class T> struct S2 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <class U> int S2<int>::g(U y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 3
// Form template<> template<class U> ..., the contract of the member template
// in the class, the specialization has a different contract.
template <class T> struct S3 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <class U> int S3<int>::g(U y) [[edg::r_pre(y > 5)]] { return 1; }
#elif TEST_NUMBER == 4
// Form template<> template<class U> ..., the contract of the member template
// on its definition outside the class, the specialization has no contract.
template <class T> struct S4 { template <class U> int g(U y); };
template <class T> template <class U> int S4<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <class U> int S4<int>::g(U y) { return 1; }
#elif TEST_NUMBER == 5
// Form template<> template<class U> ..., the contract of the member template
// on its definition outside the class, the specialization has the same
// contract.
template <class T> struct S5 { template <class U> int g(U y); };
template <class T> template <class U> int S5<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <class U> int S5<int>::g(U y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 6
// Form template<> template<class U> ..., the contract of the member template
// on its definition outside the class, the specialization has a different
// contract.
template <class T> struct S6 { template <class U> int g(U y); };
template <class T> template <class U> int S6<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <class U> int S6<int>::g(U y) [[edg::r_pre(y > 5)]] { return 1; }
#elif TEST_NUMBER == 7
// Form template<> template<> ...<long>, the contract of the member template
// in the class, the specialization has no contract.
template <class T> struct S7 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <> int S7<int>::g<long>(long y) { return 1; }
#elif TEST_NUMBER == 8
// Form template<> template<> ...<long>, the contract of the member template
// in the class, the specialization has the same contract.
template <class T> struct S8 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <> int S8<int>::g<long>(long y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 9
// Form template<> template<> ...<long>, the contract of the member template
// in the class, the specialization has a different contract.
template <class T> struct S9 { template <class U> int g(U y) [[edg::r_pre(y > 0)]]; };
template <> template <> int S9<int>::g<long>(long y) [[edg::r_pre(y > 5)]] { return 1; }
#elif TEST_NUMBER == 10
// Form template<> template<> ...<long>, the contract of the member template
// on its definition outside the class, the specialization has no contract.
template <class T> struct S10 { template <class U> int g(U y); };
template <class T> template <class U> int S10<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <> int S10<int>::g<long>(long y) { return 1; }
#elif TEST_NUMBER == 11
// Form template<> template<> ...<long>, the contract of the member template
// on its definition outside the class, the specialization has the same
// contract.
template <class T> struct S11 { template <class U> int g(U y); };
template <class T> template <class U> int S11<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <> int S11<int>::g<long>(long y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 12
// Form template<> template<> ...<long>, the contract of the member template
// on its definition outside the class, the specialization has a different
// contract.
template <class T> struct S12 { template <class U> int g(U y); };
template <class T> template <class U> int S12<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
template <> template <> int S12<int>::g<long>(long y) [[edg::r_pre(y > 5)]] { return 1; }
#elif TEST_NUMBER == 13
// Form A, the member template has no contract: the specialization cannot add
// one.
template <class T> struct S13 { template <class U> int g(U y); };
template <> template <class U> int S13<int>::g(U y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 14
// Form B, the member template has no contract: the specialization cannot add
// one.
template <class T> struct S14 { template <class U> int g(U y); };
template <> template <> int S14<int>::g<long>(long y) [[edg::r_pre(y > 0)]] { return 1; }
#elif TEST_NUMBER == 15
// Form A, the specialization is declared before the definition that has the
// contract.
template <class T> struct S15 { template <class U> int g(U y); };
template <> template <class U> int S15<int>::g(U y) { return 1; }
template <class T> template <class U> int S15<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
#elif TEST_NUMBER == 16
// Form B, the specialization is declared before the definition that has the
// contract.
template <class T> struct S16 { template <class U> int g(U y); };
template <> template <> int S16<int>::g<long>(long y) { return 1; }
template <class T> template <class U> int S16<T>::g(U y) [[edg::r_pre(y > 0)]] { return 0; }
#endif
