//type:fn
//options:--c23:--c23:--c23:--c23:--clang_version=210000 --c23:--clang_version=210000 --c23:--clang_version=210000 --c23:--clang_version=210000 --c23:--gnu_version=150000 --c23:--gnu_version=150000 --c23:--gnu_version=150000 --c23:--gnu_version=150000 --c23
//remark:[GH #?] Errors in attribute target constraints for statements in C
//require:INCLUDE_EDG_TEST_ATTRIBUTES 1
//cases:12
//match_regex:(line 24: error: attribute\s+"edg::r_loop"\s+requires\s+an\s+iteration\s+statement|line 27: error: attribute\s+"edg::r_null"\s+requires\s+a\s+null\s+statement|line 30: error: attribute\s+"edg::r_noloop"\s+does\s+not\s+apply\s+to\s+an\s+iteration\s+statement|line 33: error: attribute\s+"edg::r_nonull"\s+does\s+not\s+apply\s+to\s+a\s+null\s+statement)
// 10/14/26  [GH #?]
//
// A statement attribute whose target has a property switch is an error when
// the statement does not have the property, in every dialect (clang and GNU
// modes only downgrade the diagnostics of the front end's own attributes).
// Every case has its own diagnostic; the expression above has an alternative
// for each case, anchored to the line of its error.  Each case is run in
// three dialects: TEST_NUMBER is the number of the case plus 4 for each
// dialect after the first (the default, clang, then GNU mode).

#define CASE ((TEST_NUMBER - 1) % 4 + 1)

#if CASE == 1
// In C, an iteration or selection statement is surrounded by a generated
// block.  An attribute in front of the statement still applies to the
// statement itself, so a constraint on the kind of statement sees a loop (or
// an if statement), not the block.  r_loop requires an iteration statement.
void f1(int n) { [[edg::r_loop(n > 0)]] if (n) { } }
#elif CASE == 2
// r_null requires a null statement.
void f2(int n) { [[edg::r_null(n > 0)]] while (n) { --n; } }
#elif CASE == 3
// r_noloop does not apply to an iteration statement.
void f3(int n) { [[edg::r_noloop(n > 0)]] for (int i = 0; i < n; ++i) { } }
#elif CASE == 4
// r_nonull does not apply to a null statement.
void f4(void) { [[edg::r_nonull(1)]]; }
#endif
