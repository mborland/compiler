//type:fn
//options_all:--c23
//remark:[GH #?] Errors in registered attributes in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:5
//match_regex:(line 15: error: unknown\s+attribute\s+"edg::r_stmtx"|line 18: error: identifier\s+"undeclared_c2"\s+is\s+undefined|line 21: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here|line 24: error: attribute\s+"edg::r_fn"\s+does\s+not\s+take\s+arguments|line 28: error: attribute\s+"edg::r_stmt"\s+does\s+not\s+apply\s+here)
// 10/14/26  [GH #?]
//
// Registered attributes are scanned in C23 as in C++ (see GH-reg-err).  Every
// case has its own diagnostic, named by an alternative of the expression
// above that is anchored to the line of the case.

#if TEST_NUMBER == 1
// A name that is not registered in a closed namespace is an error.
void f1(void) { [[edg::r_stmtx]]; }
#elif TEST_NUMBER == 2
// An undeclared name in an argument.
void f2(void) { [[edg::r_stmt(undeclared_c2)]]; }
#elif TEST_NUMBER == 3
// A routine attribute on a variable.
[[edg::r_fn]] int v3;
#elif TEST_NUMBER == 4
// Arguments on an attribute that takes none.
[[edg::r_fn(1)]] int f4(void);
#elif TEST_NUMBER == 5
// The attribute of a function after its parameter list (a statement
// attribute is on the function type, where it does not apply).
int f5(int x) [[edg::r_stmt(x > 0)]];
#endif
