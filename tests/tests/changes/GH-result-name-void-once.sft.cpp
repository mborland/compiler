//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A named result that reaches an instance that returns void is one error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:10
//match_regex:(line 25: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 28: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 32: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 35: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 39: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 44: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 46: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 50: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 53: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void|line 56: error: attribute\s+"edg::r_post"\s+cannot\s+name\s+the\s+result\s+of\s+a\s+function\s+that\s+returns\s+void)
//match_regex:\A(?![\s\S]*(error:\s+attribute\s+"edg::r_post"\s+cannot\s+name[\s\S]*){2})(?![\s\S]*does\s+not\s+apply\s+here)
// 10/15/26  [GH #?]
//
// A name for the result of a function that is dependent when the attribute is
// scanned (the return type is T) is an error when an instance returns void,
// and it is one error for the attribute: for the member of a class template
// defined outside the class (with the instance of the class created before or
// after the definition), a member template, an explicit specialization or
// instance, a hidden friend, a function template and an explicit instantiation
// of it.  (The scan of the declaration for the instance and the copy of the
// attribute to it both find the error; the second regular expression above
// fails if there is more than one, or if the attribute also gets "does not
// apply here".)  Every case has its own diagnostic; the first has an
// alternative for each case, anchored to the line of its error.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> struct S { T get(); };
template<class T> T S<T>::get() E(r, true) {}
S<void> *p; void u() { S<void> s; (void)s; }
#elif TEST_NUMBER == 2
template<class T> struct S { template<class U> U g() E(r, true); };
void u() { S<int> s; s.g<void>(); }
#elif TEST_NUMBER == 3
template<class T> struct S { template<class U> U g(); };
template<class T> template<class U> U S<T>::g() E(r, true) {}
void u() { S<int> s; s.g<void>(); }
#elif TEST_NUMBER == 4
template<class T> T f() E(r, true);
template<> void f<void>() {}
#elif TEST_NUMBER == 5
template<class T> struct S { T get(); };
template<class T> T S<T>::get() E(r, true) { return T(); }
template<> void S<void>::get() {}
#elif TEST_NUMBER == 6
template<class T> struct S { T get(); };
S<void> s6;
template<class T> T S<T>::get() E(r, true) {}
#elif TEST_NUMBER == 7
template<class T> struct A { friend T fr(A) E(r, true) {} };
A<void> a;
#elif TEST_NUMBER == 8
template<class T> struct S { T get(); };
template<class T> T S<T>::get() E(r, true) {}
extern template struct S<void>;
#elif TEST_NUMBER == 9
template <class T> T g9() E(r, true);
void u9() { g9<void>(); }
#elif TEST_NUMBER == 10
template <class T> T g10() E(r, true);
template void g10<void>();
#endif
