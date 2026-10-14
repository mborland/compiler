//type:fn
//options_all:--c++20
//remark:[GH #?] No cascade of errors after an error in an argument of a registered attribute
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:2
//match_regex:(line 17: error: attribute\s+"edg::r_stmt"\s+cannot\s+be\s+a\s+pack\s+expansion|line 20: error: attribute\s+"edg::r_stmt"\s+cannot\s+be\s+a\s+pack\s+expansion)
//match_regex:\A(?![\s\S]*error:\s+expected\s+a\s+"\)")
// 10/14/26  [GH #?]
//
// Once an error has been issued for an argument of a registered attribute (here
// that it is a pack expansion), the other arguments are skipped, so there is no
// second error for the rest of the list (the second regular expression above
// fails if there is one).

#if TEST_NUMBER == 1
// A pack expansion followed by another argument.
template <class... T> void f(T... a) { [[edg::r_stmt((a > 0)..., true)]]; }
#elif TEST_NUMBER == 2
// A pack expansion alone.
template <class... T> void g(T... a) { [[edg::r_stmt((a > 0)...)]]; }
#endif
