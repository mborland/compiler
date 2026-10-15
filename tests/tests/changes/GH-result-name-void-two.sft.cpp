//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] Two named results that reach an instance that returns void are one error each
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:3
//match_regex:\A(?=(?:[\s\S]*error:\s+attribute\s+"edg::r_post"\s+cannot\s+name){2})(?![\s\S]*(?:error:\s+attribute\s+"edg::r_post"\s+cannot\s+name[\s\S]*){3})(?![\s\S]*does\s+not\s+apply\s+here)
// 10/15/26  [GH #?]
//
// The error that an attribute cannot name the result of a function that
// returns void is issued once for each attribute, also when a declaration has
// two such attributes and the attributes are copied for the instances: the
// copies of the first attribute used to be reported again after the second one
// (the front end remembered only the latest attribute).  The regular
// expression above needs two errors and fails on a third.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class T> T f() E(r, true) E(q, true);
void w() { f<void>(); }
#elif TEST_NUMBER == 2
template<class T, class U> T f(U) E(r, true) E(q, true);
void w() { f<void>(1); f<void>(1L); }
#elif TEST_NUMBER == 3
template<class T> struct S { T get(); };
template<class T> T S<T>::get() E(r, true) E(q, true) {}
S<void> s3;
#endif
