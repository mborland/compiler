//type:fn
//options:--clang_version=210000:--gnu_version=150000:--clang_version=210000 -w:--gnu_version=150000 -w
//options_all:--c23
//remark:[GH #?] Misplaced registered attributes in clang and GNU modes (C)
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//match_regex:line 20: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 23: error: attributes\s+ignored\s+here\s+because\s+they\s+do\s+not\s+apply\s+to\s+a\s+declared\s+entity
//match_regex:line 26: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
//match_regex:line 29: error: attributes\s+are\s+not\s+allowed\s+here
//match_regex:line 32: error: attributes\s+are\s+not\s+allowed\s+here
//match_regex:line 35: error: attribute\s+"edg::r_fn"\s+does\s+not\s+apply\s+here
// 10/14/26  [GH #?]
//
// As in C++ (see GH-reg-err-compat), a misplaced registered attribute is an
// error in clang and GNU modes, never dropped silently, and "-w" does not hide
// it.  Each regular expression above names one case, at its line.


// A routine attribute on a variable.
[[edg::r_fn]] int v1;

// An attribute on an empty member declaration.
struct S2 { [[edg::r_fn]]; int a; };

// An attribute on a null statement.
void f3(void) { [[edg::r_fn]]; }

// An attribute in front of a static_assert declaration.
[[edg::r_fn]] static_assert(1, "");

// An attribute in front of a static_assert declaration in a block.
void f5(void) { [[edg::r_stmt(1)]] static_assert(1, ""); }

// An attribute after a type specifier.
void f6(void) { int [[edg::r_fn]] x = 0; (void)x; }
