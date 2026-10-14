//type:fn
//options:--c17:--c23:--c17 --clang_version=210000:--c23 --clang_version=210000:--c17 --gnu_version=150000:--c23 --gnu_version=150000
//remark:[GH #?] A registered attribute after the identifier list of an old-style function definition
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:1
//match_regex:line 19: error: (expected\s+an\s+expression|attribute\s+"edg::r_pre"\s+does\s+not\s+apply\s+here|identifier-only\s+parameters\s+can\s+only\s+be\s+used\s+in\s+old-style\s+function\s+definitions)
//match_regex:\A(?![\s\S]*internal\s+error)
// 10/14/26  [GH #?]
//
// The identifier list of an old-style (K&R) function definition is not a
// parameter-declaration clause: a registered attribute after it is not a
// contract of the function (its parameters have no types yet), it is
// attached to the function type, where it is an error in every language
// mode (it must not be accepted, and must not crash the front end).  The
// expression above gives the acceptable diagnostics, all at the line of the
// function.

int x = 5;
int f2(x) [[edg::r_pre(x > 0)]] int x; { return x; }
