//type:fn
//options_all:--c++20 --instantiate=used
//remark:[GH #?] A contract that refers to a function parameter pack is one error
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:8
//match_regex:(line 23: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 26: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 29: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 32: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 35: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 38: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 41: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack|line 44: error: attribute\s+"edg::r_(pre|post)"\s+cannot\s+refer\s+to\s+a\s+function\s+parameter\s+pack)
//match_regex:\A(?![\s\S]*cannot\s+refer\s+to\s+a\s+function\s+parameter[\s\S]*cannot\s+refer\s+to\s+a\s+function\s+parameter)
// 10/15/26  [GH #?]
//
// A contract that refers to a function parameter pack is an error once, in the
// prototype of the template, when it is not instantiated, when it is
// instantiated with one element, several, or none, and for a definition and
// an explicit instantiation; the attribute is not scanned again for the
// instances (an error was reported for it already), so there is no error
// "detected during instantiation", and no error for the parameters that follow
// the pack.  The second regular expression above fails if there is more than
// one error.  Every case has its own diagnostic; the first has an alternative
// for each case, anchored to the line of its error.

#define E(...) [[edg::r_post(__VA_ARGS__)]]
#define Q(...) [[edg::r_pre(__VA_ARGS__)]]
#if TEST_NUMBER == 1
template<class... T> int f(T... xs) Q(((xs > 0) && ...));
int u() { return f(1, 2); }
#elif TEST_NUMBER == 2
template<class... T> int f(T... xs) Q(((xs > 0) && ...)) { return 0; }
int u() { return f(1, 2); }
#elif TEST_NUMBER == 3
template<class... T> int f(T... xs) Q(((xs > 0) && ...)) { return 0; }
int u() { return f(1); }
#elif TEST_NUMBER == 4
template<class... T> int f(T... xs) Q(((xs > 0) && ...)) { return 0; }
int u() { return f(); }
#elif TEST_NUMBER == 5
template<class... T> int f(T... xs) Q(sizeof...(xs) > 0) { return 0; }
int u() { return f(1, 2, 3); }
#elif TEST_NUMBER == 6
template<class... T> int f(int a, T... xs) Q(((xs > a) && ...)) { return 0; }
int u() { return f(0, 1, 2); }
#elif TEST_NUMBER == 7
template<class... T> int f(T... xs) E(r, ((r > xs) && ...)) { return 0; }
int u() { return f(1, 2); }
#elif TEST_NUMBER == 8
template<class... T> int f(T... xs) Q(((xs > 0) && ...)) { return 0; }
template int f<int, int>(int, int);
#endif
