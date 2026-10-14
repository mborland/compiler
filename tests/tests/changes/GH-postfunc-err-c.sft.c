//type:fn
//options_all:--c23
//remark:[GH #?] Errors in registered attributes after the parameter list in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:5
//match_regex:(line 16: error: attribute\s+"edg::r_pre"\s+does\s+not\s+match\s+the\s+attributes\s+of\s+an\s+earlier\s+declaration|line 19: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 22: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 25: error: attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|line 28: error: identifier\s+"x5"\s+is\s+undefined)
// 10/14/26  [GH #?]
//
// The errors of GH-postfunc-err that apply in C.  Every case has its own
// diagnostic; the expression above has an alternative for each case,
// anchored to the line of its error.

#if TEST_NUMBER == 1
// A different predicate on a later declaration.
int e1(int x) [[edg::r_pre(x > 0)]];
int e1(int x) [[edg::r_pre(x > 1)]];
#elif TEST_NUMBER == 2
// A pointer to function (the attribute is on the type, not a function).
int (*pf2)(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 3
// A function typedef.
typedef int F3(int x) [[edg::r_pre(x > 0)]];
#elif TEST_NUMBER == 4
// A function that returns a pointer to function: the attribute is not the function's.
int (*e4(int x))(double d) [[edg::r_pre(d > 0)]];
#elif TEST_NUMBER == 5
// The attribute in front of the declaration cannot name the parameters.
[[edg::r_pre(x5 > 0)]] int e5(int x5);
#endif
